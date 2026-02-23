// DirectInput axis/button remapping for steering wheels and custom controllers.
// Hooks the game's GetVolume/SwitchOn/SwitchNow functions to read from
// user-configured DirectInput devices with custom axis/button mapping.
// Supports up to 3 devices: Primary (wheel), Shifter, Aux (button box).
// Mutually exclusive with UseNewInput (SDL3-based input).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dinput.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"

// Defined in Proxy.cpp — the real IDirectInput8A before our filtering wrapper
extern IDirectInput8A* g_RealDirectInput8;

namespace DInputRemap
{
	// ---------- Device slot ----------

	struct DeviceSlot
	{
		IDirectInputDevice8A* device = nullptr;
		DIJOYSTATE2 currentState = {};
		DIJOYSTATE2 previousState = {};
		std::string name;        // for log messages
		bool initialized = false;
		bool initAttempted = false;
	};

	static DeviceSlot primary;
	static DeviceSlot shifter;
	static DeviceSlot aux;

	// Overall init state (true once primary succeeds)
	static bool initialized = false;
	static bool initAttempted = false;
	static DWORD lastPollFrame = 0;
	static uint32_t prevKeyboardMask = 0;

	// GUIDs of devices already opened — used to skip during auto-detect
	static std::vector<GUID> openedGuids;

	// ---------- H-pattern shifter state machine ----------

	struct HPatternState
	{
		int targetGear = 0;       // 0=neutral, 1-6=forward, -1=reverse
		bool shiftCooldown = false;
	};
	static HPatternState hpattern;

	// ---------- Axis helpers ----------

	static LONG ReadAxisRaw(const DIJOYSTATE2& state, int index)
	{
		switch (index)
		{
		case 0: return state.lX;
		case 1: return state.lY;
		case 2: return state.lZ;
		case 3: return state.lRx;
		case 4: return state.lRy;
		case 5: return state.lRz;
		case 6: return state.rglSlider[0];
		case 7: return state.rglSlider[1];
		default: return 0;
		}
	}

	static const char* AxisName(int index)
	{
		switch (index)
		{
		case 0: return "lX";
		case 1: return "lY";
		case 2: return "lZ";
		case 3: return "lRx";
		case 4: return "lRy";
		case 5: return "lRz";
		case 6: return "Slider0";
		case 7: return "Slider1";
		default: return "?";
		}
	}

	// ---------- Device enumeration ----------

	struct DeviceCandidate
	{
		GUID guid;
		char name[MAX_PATH];
		DWORD axisCount;
	};

	struct EnumContext
	{
		IDirectInput8A* di;
		DeviceCandidate best;
		bool found;
	};

	static BOOL CALLBACK EnumDevicesCallback(const DIDEVICEINSTANCEA* inst, VOID* ctx)
	{
		auto* ec = static_cast<EnumContext*>(ctx);
		spdlog::info("DInputRemap: Found device: '{}' GUID={{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
			inst->tszInstanceName,
			inst->guidInstance.Data1, inst->guidInstance.Data2, inst->guidInstance.Data3,
			inst->guidInstance.Data4[0], inst->guidInstance.Data4[1],
			inst->guidInstance.Data4[2], inst->guidInstance.Data4[3],
			inst->guidInstance.Data4[4], inst->guidInstance.Data4[5],
			inst->guidInstance.Data4[6], inst->guidInstance.Data4[7]);

		// Skip vJoy virtual devices
		std::string name(inst->tszInstanceName);
		if (name.find("vJoy") != std::string::npos)
		{
			spdlog::info("DInputRemap:   (skipping virtual device)");
			return DIENUM_CONTINUE;
		}

		// Skip devices already opened by another slot
		for (const auto& g : openedGuids)
		{
			if (IsEqualGUID(inst->guidInstance, g))
			{
				spdlog::info("DInputRemap:   (skipping — already opened by another slot)");
				return DIENUM_CONTINUE;
			}
		}

		// Query axis count to rank candidates — wheels have 3+ axes
		DWORD axisCount = 0;
		IDirectInputDevice8A* tmpDev = nullptr;
		if (SUCCEEDED(ec->di->CreateDevice(inst->guidInstance, &tmpDev, nullptr)))
		{
			tmpDev->SetDataFormat(&c_dfDIJoystick2);
			DIDEVCAPS caps = {};
			caps.dwSize = sizeof(DIDEVCAPS);
			if (SUCCEEDED(tmpDev->GetCapabilities(&caps)))
				axisCount = caps.dwAxes;
			tmpDev->Release();
		}
		spdlog::info("DInputRemap:   {} axes", axisCount);

		// Prefer the device with the most axes (steering wheel > shifter > button box)
		if (!ec->found || axisCount > ec->best.axisCount)
		{
			ec->best.guid = inst->guidInstance;
			strncpy_s(ec->best.name, inst->tszInstanceName, _TRUNCATE);
			ec->best.axisCount = axisCount;
			ec->found = true;
		}

		return DIENUM_CONTINUE; // Keep enumerating to find the best device
	}

	// Callback to set axis range on all axes
	static BOOL CALLBACK EnumObjectsCallback(const DIDEVICEOBJECTINSTANCEA* obj, VOID* ctx)
	{
		auto* dev = static_cast<IDirectInputDevice8A*>(ctx);
		if (obj->dwType & DIDFT_AXIS)
		{
			DIPROPRANGE range;
			range.diph.dwSize = sizeof(DIPROPRANGE);
			range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			range.diph.dwHow = DIPH_BYID;
			range.diph.dwObj = obj->dwType;
			range.lMin = 0;
			range.lMax = 65535;
			dev->SetProperty(DIPROP_RANGE, &range.diph);
		}
		return DIENUM_CONTINUE;
	}

	// Parse GUID string like "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" or "auto"
	static bool ParseGuid(const std::string& str, GUID& out)
	{
		if (str == "auto" || str.empty())
			return false;

		wchar_t wide[64];
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wide, 64);
		return SUCCEEDED(CLSIDFromString(wide, &out));
	}

	// ---------- Init a single device slot ----------

	static bool InitSlot(DeviceSlot& slot, const std::string& guidStr, const char* slotName, IDirectInput8A* di)
	{
		if (slot.initAttempted)
			return slot.initialized;
		slot.initAttempted = true;
		slot.name = slotName;

		spdlog::info("DInputRemap: Initializing {} slot...", slotName);

		GUID targetGuid = {};
		bool guidSpecified = ParseGuid(guidStr, targetGuid);

		if (!guidSpecified)
		{
			// Auto-detect: enumerate all controllers, pick the one with the most axes
			EnumContext ctx = {};
			ctx.di = di;
			ctx.found = false;
			ctx.best = {};
			di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumDevicesCallback, &ctx, DIEDFL_ATTACHEDONLY);
			if (!ctx.found)
			{
				spdlog::warn("DInputRemap: {} — no available game controller found", slotName);
				return false;
			}
			targetGuid = ctx.best.guid;
			spdlog::info("DInputRemap: {} auto-selected: '{}' ({} axes)", slotName, ctx.best.name, ctx.best.axisCount);
		}

		// Create and configure the device
		HRESULT hr = di->CreateDevice(targetGuid, &slot.device, nullptr);
		if (FAILED(hr))
		{
			spdlog::error("DInputRemap: {} CreateDevice failed (HRESULT 0x{:08X})", slotName, (unsigned)hr);
			return false;
		}

		hr = slot.device->SetDataFormat(&c_dfDIJoystick2);
		if (FAILED(hr))
		{
			spdlog::error("DInputRemap: {} SetDataFormat failed (HRESULT 0x{:08X})", slotName, (unsigned)hr);
			slot.device->Release();
			slot.device = nullptr;
			return false;
		}

		// Non-exclusive background access
		hr = slot.device->SetCooperativeLevel(Game::GameHwnd(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
		if (FAILED(hr))
		{
			spdlog::error("DInputRemap: {} SetCooperativeLevel failed (HRESULT 0x{:08X})", slotName, (unsigned)hr);
			slot.device->Release();
			slot.device = nullptr;
			return false;
		}

		// Set all axes to 0..65535 range
		slot.device->EnumObjects(EnumObjectsCallback, slot.device, DIDFT_AXIS);

		hr = slot.device->Acquire();
		if (FAILED(hr) && hr != DIERR_OTHERAPPHASPRIO)
		{
			spdlog::error("DInputRemap: {} Acquire failed (HRESULT 0x{:08X})", slotName, (unsigned)hr);
			slot.device->Release();
			slot.device = nullptr;
			return false;
		}

		// Log capabilities
		DIDEVCAPS caps = {};
		caps.dwSize = sizeof(DIDEVCAPS);
		slot.device->GetCapabilities(&caps);
		spdlog::info("DInputRemap: {} opened — {} axes, {} buttons, {} POVs",
			slotName, caps.dwAxes, caps.dwButtons, caps.dwPOVs);

		// Track opened GUID so other slots skip it during auto-detect
		openedGuids.push_back(targetGuid);

		slot.initialized = true;
		return true;
	}

	// ---------- Deferred init (called on first frame) ----------

	static bool DeferredInit()
	{
		if (initAttempted)
			return initialized;
		initAttempted = true;

		spdlog::info("DInputRemap: Starting deferred initialization...");

		auto* di = g_RealDirectInput8 ? g_RealDirectInput8 : Game::DirectInput8();
		if (!di)
		{
			spdlog::error("DInputRemap: IDirectInput8 not available yet");
			initAttempted = false; // Allow retry next frame
			return false;
		}

		// Primary slot (required — fail if it can't open)
		if (!InitSlot(primary, Settings::DIRemapDeviceGuid, "Primary", di))
			return false;

		spdlog::info("DInputRemap: Primary mapping — Steering={}({}), Accel={}({}), Brake={}({})",
			AxisName(Settings::DIRemapSteeringAxis), Settings::DIRemapSteeringInvert ? "inv" : "norm",
			AxisName(Settings::DIRemapAccelAxis), Settings::DIRemapAccelInvert ? "inv" : "norm",
			AxisName(Settings::DIRemapBrakeAxis), Settings::DIRemapBrakeInvert ? "inv" : "norm");

		// Shifter slot (optional)
		if (Settings::DIShifterEnabled)
		{
			if (InitSlot(shifter, Settings::DIShifterDeviceGuid, "Shifter", di))
				spdlog::info("DInputRemap: Shifter mode: {}", Settings::DIShifterGearMode);
			else
				spdlog::warn("DInputRemap: Shifter slot configured but failed to open");
		}

		// Aux slot (optional)
		if (Settings::DIAuxEnabled)
		{
			if (!InitSlot(aux, Settings::DIAuxDeviceGuid, "Aux", di))
				spdlog::warn("DInputRemap: Aux slot configured but failed to open");
		}

		initialized = true;
		return true;
	}

	// ---------- Polling ----------

	static void PollSlot(DeviceSlot& slot)
	{
		if (!slot.device) return;
		slot.previousState = slot.currentState;

		HRESULT hr = slot.device->Poll();
		if (FAILED(hr))
			hr = slot.device->Acquire();

		hr = slot.device->GetDeviceState(sizeof(DIJOYSTATE2), &slot.currentState);
		if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
		{
			slot.device->Acquire();
			slot.device->GetDeviceState(sizeof(DIJOYSTATE2), &slot.currentState);
		}
	}

	// ---------- H-pattern shifter logic ----------

	static void UpdateHPattern()
	{
		if (Settings::DIShifterGearMode != "hpattern" || !shifter.device)
			return;

		// Read which gear button is pressed on the shifter device (mutually exclusive)
		auto isPressed = [](const DeviceSlot& s, int btn) -> bool {
			return btn >= 0 && btn < 128 && (s.currentState.rgbButtons[btn] & 0x80) != 0;
		};

		int target = 0;
		if (isPressed(shifter, Settings::DIShifterButtonGear1))           target = 1;
		else if (isPressed(shifter, Settings::DIShifterButtonGear2))      target = 2;
		else if (isPressed(shifter, Settings::DIShifterButtonGear3))      target = 3;
		else if (isPressed(shifter, Settings::DIShifterButtonGear4))      target = 4;
		else if (isPressed(shifter, Settings::DIShifterButtonGear5))      target = 5;
		else if (isPressed(shifter, Settings::DIShifterButtonGear6))      target = 6;
		else if (isPressed(shifter, Settings::DIShifterButtonGearReverse)) target = -1;
		// else target = 0 (neutral — no shifts emitted)

		hpattern.targetGear = target;
	}

	static void Poll()
	{
		// Guard: only poll once per frame (GetVolume called 3+ times per frame)
		DWORD tick = GetTickCount();
		if (tick == lastPollFrame)
			return;
		lastPollFrame = tick;

		PollSlot(primary);
		PollSlot(shifter);
		PollSlot(aux);
		UpdateHPattern();
	}

	// ---------- Axis reading (primary slot only) ----------

	static int GetSteering()
	{
		LONG raw = ReadAxisRaw(primary.currentState, Settings::DIRemapSteeringAxis);
		float normalized = (static_cast<float>(raw) - 32767.5f) / 32767.5f; // -1.0 to +1.0
		if (Settings::DIRemapSteeringInvert)
			normalized = -normalized;

		// Apply deadzone
		float dz = Settings::SteeringDeadZone;
		if (dz > 0.0f && std::abs(normalized) < dz)
			return 0;
		if (dz > 0.0f)
		{
			float sign = normalized > 0.0f ? 1.0f : -1.0f;
			normalized = sign * (std::abs(normalized) - dz) / (1.0f - dz);
		}

		// Apply sensitivity multiplier
		normalized *= Settings::DIRemapSteeringSensitivity;

		return static_cast<int>(std::clamp(normalized * 127.0f, -127.0f, 127.0f));
	}

	static int GetAcceleration()
	{
		LONG raw = ReadAxisRaw(primary.currentState, Settings::DIRemapAccelAxis);
		float normalized = static_cast<float>(raw) / 65535.0f;
		if (Settings::DIRemapAccelInvert)
			normalized = 1.0f - normalized;
		return static_cast<int>(std::clamp(normalized * 255.0f, 0.0f, 255.0f));
	}

	static int GetBrake()
	{
		LONG raw = ReadAxisRaw(primary.currentState, Settings::DIRemapBrakeAxis);
		float normalized = static_cast<float>(raw) / 65535.0f;
		if (Settings::DIRemapBrakeInvert)
			normalized = 1.0f - normalized;
		return static_cast<int>(std::clamp(normalized * 255.0f, 0.0f, 255.0f));
	}

	// ---------- Button checking (multi-slot) ----------

	// Check if a button is pressed on a given slot
	static bool IsButtonPressed(const DeviceSlot& slot, int buttonIndex)
	{
		if (!slot.device || buttonIndex < 0 || buttonIndex >= 128)
			return false;
		return (slot.currentState.rgbButtons[buttonIndex] & 0x80) != 0;
	}

	static bool WasButtonPressed(const DeviceSlot& slot, int buttonIndex)
	{
		if (!slot.device || buttonIndex < 0 || buttonIndex >= 128)
			return false;
		return (slot.previousState.rgbButtons[buttonIndex] & 0x80) != 0;
	}

	// Get button index for a SwitchId on the primary slot
	static int ButtonForSwitchPrimary(SwitchId id)
	{
		switch (id)
		{
		case SwitchId::Start:          return Settings::DIRemapButtonStart;
		case SwitchId::Back:           return Settings::DIRemapButtonBack;
		case SwitchId::A:              return Settings::DIRemapButtonA;
		case SwitchId::B:              return Settings::DIRemapButtonB;
		case SwitchId::X:              return Settings::DIRemapButtonX;
		case SwitchId::Y:              return Settings::DIRemapButtonY;
		case SwitchId::GearUp:         return Settings::DIRemapButtonGearUp;
		case SwitchId::GearDown:       return Settings::DIRemapButtonGearDown;
		case SwitchId::ChangeView:     return Settings::DIRemapButtonChangeView;
		case SwitchId::SelectionUp:    return Settings::DIRemapButtonSelUp;
		case SwitchId::SelectionDown:  return Settings::DIRemapButtonSelDown;
		case SwitchId::SelectionLeft:  return Settings::DIRemapButtonSelLeft;
		case SwitchId::SelectionRight: return Settings::DIRemapButtonSelRight;
		default:                       return -1;
		}
	}

	// Get button index for a SwitchId on the aux slot
	static int ButtonForSwitchAux(SwitchId id)
	{
		switch (id)
		{
		case SwitchId::Start:          return Settings::DIAuxButtonStart;
		case SwitchId::Back:           return Settings::DIAuxButtonBack;
		case SwitchId::A:              return Settings::DIAuxButtonA;
		case SwitchId::B:              return Settings::DIAuxButtonB;
		case SwitchId::X:              return Settings::DIAuxButtonX;
		case SwitchId::Y:              return Settings::DIAuxButtonY;
		case SwitchId::GearUp:         return Settings::DIAuxButtonGearUp;
		case SwitchId::GearDown:       return Settings::DIAuxButtonGearDown;
		case SwitchId::ChangeView:     return Settings::DIAuxButtonChangeView;
		case SwitchId::SelectionUp:    return Settings::DIAuxButtonSelUp;
		case SwitchId::SelectionDown:  return Settings::DIAuxButtonSelDown;
		case SwitchId::SelectionLeft:  return Settings::DIAuxButtonSelLeft;
		case SwitchId::SelectionRight: return Settings::DIAuxButtonSelRight;
		default:                       return -1;
		}
	}

	// Get button index for GearUp/GearDown on the shifter slot (sequential mode)
	static int ButtonForSwitchShifter(SwitchId id)
	{
		switch (id)
		{
		case SwitchId::GearUp:   return Settings::DIShifterButtonGearUp;
		case SwitchId::GearDown: return Settings::DIShifterButtonGearDown;
		default:                 return -1;
		}
	}

	// Check if a SwitchId button is pressed on any slot
	static bool IsButtonPressedAny(SwitchId id)
	{
		// Primary
		if (IsButtonPressed(primary, ButtonForSwitchPrimary(id)))
			return true;
		// Aux
		if (IsButtonPressed(aux, ButtonForSwitchAux(id)))
			return true;
		// Shifter (sequential mode GearUp/GearDown only)
		if (Settings::DIShifterGearMode == "sequential")
		{
			if (IsButtonPressed(shifter, ButtonForSwitchShifter(id)))
				return true;
		}
		return false;
	}

	// Check if a SwitchId button was pressed on any slot (previous frame)
	static bool WasButtonPressedAny(SwitchId id)
	{
		if (WasButtonPressed(primary, ButtonForSwitchPrimary(id)))
			return true;
		if (WasButtonPressed(aux, ButtonForSwitchAux(id)))
			return true;
		if (Settings::DIShifterGearMode == "sequential")
		{
			if (WasButtonPressed(shifter, ButtonForSwitchShifter(id)))
				return true;
		}
		return false;
	}

	// ---------- Keyboard fallback ----------

	static uint32_t GetKeyboardMask()
	{
		uint32_t mask = 0;
		if (GetAsyncKeyState(VK_UP) & 0x8000)    mask |= (1 << static_cast<int>(SwitchId::SelectionUp));
		if (GetAsyncKeyState(VK_DOWN) & 0x8000)   mask |= (1 << static_cast<int>(SwitchId::SelectionDown));
		if (GetAsyncKeyState(VK_LEFT) & 0x8000)   mask |= (1 << static_cast<int>(SwitchId::SelectionLeft));
		if (GetAsyncKeyState(VK_RIGHT) & 0x8000)  mask |= (1 << static_cast<int>(SwitchId::SelectionRight));
		if (GetAsyncKeyState(VK_RETURN) & 0x8000)  mask |= (1 << static_cast<int>(SwitchId::A));
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)  mask |= (1 << static_cast<int>(SwitchId::B));
		if (GetAsyncKeyState(VK_SPACE) & 0x8000)   mask |= (1 << static_cast<int>(SwitchId::Start));
		if (GetAsyncKeyState(VK_BACK) & 0x8000)    mask |= (1 << static_cast<int>(SwitchId::Back));
		return mask;
	}

	// ---------- POV hat (merged across all slots) ----------

	static void ApplyPovToMask(const DeviceSlot& slot, uint32_t& mask)
	{
		if (!slot.device) return;
		DWORD pov = slot.currentState.rgdwPOV[0];
		if (pov == 0xFFFFFFFF) return;
		if (pov >= 31500 || pov <= 4500)  mask |= (1 << static_cast<int>(SwitchId::SelectionUp));
		if (pov >= 4500 && pov <= 13500)  mask |= (1 << static_cast<int>(SwitchId::SelectionRight));
		if (pov >= 13500 && pov <= 22500) mask |= (1 << static_cast<int>(SwitchId::SelectionDown));
		if (pov >= 22500 && pov <= 31500) mask |= (1 << static_cast<int>(SwitchId::SelectionLeft));
	}

	static void ApplyPovEdgeToMask(const DeviceSlot& slot, uint32_t& mask)
	{
		if (!slot.device) return;
		DWORD pov = slot.currentState.rgdwPOV[0];
		DWORD prevPov = slot.previousState.rgdwPOV[0];
		if (pov == prevPov || pov == 0xFFFFFFFF) return;
		if (pov >= 31500 || pov <= 4500)  mask |= (1 << static_cast<int>(SwitchId::SelectionUp));
		if (pov >= 4500 && pov <= 13500)  mask |= (1 << static_cast<int>(SwitchId::SelectionRight));
		if (pov >= 13500 && pov <= 22500) mask |= (1 << static_cast<int>(SwitchId::SelectionDown));
		if (pov >= 22500 && pov <= 31500) mask |= (1 << static_cast<int>(SwitchId::SelectionLeft));
	}

	// ---------- Switch mask builders ----------

	static uint32_t BuildSwitchMask()
	{
		uint32_t mask = 0;

		// Buttons from all slots
		for (int i = 0; i < static_cast<int>(SwitchId::Count); i++)
		{
			SwitchId id = static_cast<SwitchId>(i);
			if (IsButtonPressedAny(id))
				mask |= (1 << i);
		}

		// POV hat from all slots
		ApplyPovToMask(primary, mask);
		ApplyPovToMask(shifter, mask);
		ApplyPovToMask(aux, mask);

		// Keyboard fallback
		mask |= GetKeyboardMask();

		return mask;
	}

	static uint32_t BuildSwitchOnMask()
	{
		uint32_t mask = 0;

		// Edge detection for buttons on all slots
		for (int i = 0; i < static_cast<int>(SwitchId::Count); i++)
		{
			SwitchId id = static_cast<SwitchId>(i);
			if (IsButtonPressedAny(id) && !WasButtonPressedAny(id))
				mask |= (1 << i);
		}

		// H-pattern synthetic GearUp/GearDown edges
		if (Settings::DIShifterGearMode == "hpattern" && hpattern.targetGear != 0 && Game::is_in_game())
		{
			EVWORK_CAR* car = Game::pl_car();
			if (car)
			{
				int currentGear = static_cast<int>(car->cur_gear_208);
				if (hpattern.targetGear > currentGear && !hpattern.shiftCooldown)
				{
					mask |= (1 << static_cast<int>(SwitchId::GearUp));
					hpattern.shiftCooldown = true;
				}
				else if (hpattern.targetGear < currentGear && !hpattern.shiftCooldown)
				{
					mask |= (1 << static_cast<int>(SwitchId::GearDown));
					hpattern.shiftCooldown = true;
				}
				else
				{
					hpattern.shiftCooldown = false;
				}
			}
		}

		// POV hat edge detection from all slots
		ApplyPovEdgeToMask(primary, mask);
		ApplyPovEdgeToMask(shifter, mask);
		ApplyPovEdgeToMask(aux, mask);

		// Keyboard edge detection — cached per frame
		static DWORD lastKbEdgeFrame = 0;
		static uint32_t cachedKbEdges = 0;
		DWORD tick = GetTickCount();
		if (tick != lastKbEdgeFrame)
		{
			uint32_t kbNow = GetKeyboardMask();
			cachedKbEdges = kbNow & ~prevKeyboardMask;
			prevKeyboardMask = kbNow;
			lastKbEdgeFrame = tick;
		}
		mask |= cachedKbEdges;

		return mask;
	}
}

class DirectInputRemapHook : public Hook
{
	// Hook the same game functions as NewInputHook
	inline static SafetyHookInline GetVolume_hook = {};
	static int __cdecl GetVolume_dest(ADChannel volumeId)
	{
		if (!DInputRemap::initialized)
		{
			if (!DInputRemap::DeferredInit())
				return GetVolume_hook.ccall<int>(volumeId);
		}

		DInputRemap::Poll();

		switch (volumeId)
		{
		case ADChannel::Steering:     return DInputRemap::GetSteering();
		case ADChannel::Acceleration: return DInputRemap::GetAcceleration();
		case ADChannel::Brake:        return DInputRemap::GetBrake();
		default:                      return GetVolume_hook.ccall<int>(volumeId);
		}
	}

	inline static SafetyHookInline GetVolumeOld_hook = {};
	static int __cdecl GetVolumeOld_dest(ADChannel volumeId)
	{
		if (!DInputRemap::initialized)
			return GetVolumeOld_hook.ccall<int>(volumeId);

		// Return values based on previous frame state
		switch (volumeId)
		{
		case ADChannel::Steering:
		{
			LONG raw = DInputRemap::ReadAxisRaw(DInputRemap::primary.previousState, Settings::DIRemapSteeringAxis);
			float n = (static_cast<float>(raw) - 32767.5f) / 32767.5f;
			if (Settings::DIRemapSteeringInvert) n = -n;
			return static_cast<int>(std::clamp(n * 127.0f, -127.0f, 127.0f));
		}
		case ADChannel::Acceleration:
		{
			LONG raw = DInputRemap::ReadAxisRaw(DInputRemap::primary.previousState, Settings::DIRemapAccelAxis);
			float n = static_cast<float>(raw) / 65535.0f;
			if (Settings::DIRemapAccelInvert) n = 1.0f - n;
			return static_cast<int>(std::clamp(n * 255.0f, 0.0f, 255.0f));
		}
		case ADChannel::Brake:
		{
			LONG raw = DInputRemap::ReadAxisRaw(DInputRemap::primary.previousState, Settings::DIRemapBrakeAxis);
			float n = static_cast<float>(raw) / 65535.0f;
			if (Settings::DIRemapBrakeInvert) n = 1.0f - n;
			return static_cast<int>(std::clamp(n * 255.0f, 0.0f, 255.0f));
		}
		default:
			return GetVolumeOld_hook.ccall<int>(volumeId);
		}
	}

	inline static SafetyHookInline SwitchOn_hook = {};
	static int __cdecl SwitchOn_dest(uint32_t switches)
	{
		if (!DInputRemap::initialized)
		{
			if (!DInputRemap::DeferredInit())
				return SwitchOn_hook.ccall<int>(switches);
		}

		DInputRemap::Poll();
		uint32_t ourMask = DInputRemap::BuildSwitchOnMask();

		// Hybrid: suppress original for nav directions (prevents pedal axis-as-menu scrolling),
		// merge with original for everything else
		static const uint32_t navBits =
			(1 << static_cast<int>(SwitchId::SelectionUp)) |
			(1 << static_cast<int>(SwitchId::SelectionDown)) |
			(1 << static_cast<int>(SwitchId::SelectionLeft)) |
			(1 << static_cast<int>(SwitchId::SelectionRight));
		if (switches & navBits)
			return (ourMask & switches) ? 1 : 0;
		int original = SwitchOn_hook.ccall<int>(switches);
		return ((ourMask & switches) ? 1 : 0) | original;
	}

	inline static SafetyHookInline SwitchNow_hook = {};
	static int __cdecl SwitchNow_dest(uint32_t switches)
	{
		if (!DInputRemap::initialized)
		{
			if (!DInputRemap::DeferredInit())
				return SwitchNow_hook.ccall<int>(switches);
		}

		DInputRemap::Poll();
		uint32_t ourMask = DInputRemap::BuildSwitchMask();

		// Hybrid: suppress original for nav directions only
		static const uint32_t navBits =
			(1 << static_cast<int>(SwitchId::SelectionUp)) |
			(1 << static_cast<int>(SwitchId::SelectionDown)) |
			(1 << static_cast<int>(SwitchId::SelectionLeft)) |
			(1 << static_cast<int>(SwitchId::SelectionRight));
		if (switches & navBits)
			return (ourMask & switches) ? 1 : 0;
		int original = SwitchNow_hook.ccall<int>(switches);
		return ((ourMask & switches) ? 1 : 0) | original;
	}

public:
	std::string_view description() override
	{
		return "DirectInputRemap";
	}

	bool validate() override
	{
		return Settings::UseDirectInputRemap && !Settings::UseNewInput;
	}

	bool apply() override
	{
		// Hook the game's input reading functions
		GetVolume_hook = safetyhook::create_inline(Module::exe_ptr(0x53720), GetVolume_dest);
		if (!GetVolume_hook)
		{
			spdlog::error("DirectInputRemap: Failed to hook GetVolume");
			return false;
		}

		GetVolumeOld_hook = safetyhook::create_inline(Module::exe_ptr(0x53750), GetVolumeOld_dest);
		SwitchOn_hook = safetyhook::create_inline(Module::exe_ptr(0x536F0), SwitchOn_dest);
		SwitchNow_hook = safetyhook::create_inline(Module::exe_ptr(0x536C0), SwitchNow_dest);

		spdlog::info("DirectInputRemap: Hooks installed, device init deferred to first frame");
		return true;
	}

	static DirectInputRemapHook instance;
};
DirectInputRemapHook DirectInputRemapHook::instance;
