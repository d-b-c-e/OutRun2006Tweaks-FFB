// Test injection for the DirectInput remap (see remap_inject.hpp). The grammar, the table of running samples and their
// delivery are the toolkit's (src/vendor/controls/dbce_inject*.hpp), shared with the DirectInput proxies and F-Zero;
// this file holds only OutRun's side: the arming conditions, the session and command file, and the DIJOYSTATE2 view.
#include "remap_inject.hpp"

#include "profile_controls.hpp"
#include "signal_mute_policy.hpp"
#include "vendor/controls/dbce_inject_table.hpp"
#include "vendor/macro/dbce_macro_record.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>

#ifndef REMAP_INJECT_NO_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace ctl = dbce::controls;

namespace RemapInject
{
	namespace
	{
		constexpr size_t kMaxFileBytes = 4096;
		constexpr int kMaxFileCommands = 32;
		constexpr int kMaxSessionCommands = 2000;
		constexpr uint64_t kMaxSessionSeconds = 3600;

		std::atomic<bool> g_armed{ false };
		std::mutex g_session;          // the section, nonce, expiry and counts; the table has its own lock
		ctl::Section g_section;        // the applied [Controls], for "inject action"
		ctl::InjectionTable g_table;
		std::string g_nonce;
		std::atomic<uint64_t> g_expiresMs{ 0 };   // on NowMs()'s clock; 0 = no expiry (tests)
		int g_sessionCommands = 0;
		bool g_testing = false;
		uint64_t g_testClock = 0, g_nextPoll = 0;
		std::wstring g_commands;
		FILETIME g_lastWrite{};

		void Log(const std::string& line)
		{
#ifndef REMAP_INJECT_NO_SPDLOG
			spdlog::info("RemapInject: {}", line);
#else
			std::fprintf(stderr, "RemapInject: %s\n", line.c_str());
#endif
		}

		uint64_t NowMs() { return g_testing ? g_testClock : (uint64_t)GetTickCount64(); }

		std::wstring DbcePath(const wchar_t* file)
		{
			wchar_t base[MAX_PATH]{};
			const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
			if (!n || n >= MAX_PATH) return {};
			return std::wstring(base) + L"\\dbce\\outrun2006\\" + file;
		}

		// The session ends at its expiry: nothing more runs, though the process stays muted (that latch never clears).
		bool SessionLive()
		{
			if (!g_armed) return false;
			const uint64_t expires = g_expiresMs;
			if (expires && NowMs() >= expires)
			{
				g_armed = false;
				const int n = g_table.clearFor("");
				Log("test session expired; " + std::to_string(n) + " running sample(s) dropped; injection off");
				return false;
			}
			return true;
		}

		// inject.on: "nonce=<8-64 letters/digits>" and "expires=<unix seconds>" in (now, now + 1 h].
		bool ParseSession(const std::string& text, uint64_t nowUnix, std::string& nonce, uint64_t& expiresUnix, std::string& why)
		{
			nonce.clear(); expiresUnix = 0;
			if (text.size() > 512) { why = "inject.on is larger than 512 bytes"; return false; }
			std::istringstream in(text);
			std::string line;
			bool haveExpiry = false;
			while (std::getline(in, line))
			{
				const std::string t = ctl::trim(line);
				if (t.rfind("nonce=", 0) == 0) nonce = t.substr(6);
				else if (t.rfind("expires=", 0) == 0)
				{
					const std::string v = t.substr(8);
					if (v.empty() || v.size() > 12 || v.find_first_not_of("0123456789") != std::string::npos) { why = "expires= is not unix seconds"; return false; }
					expiresUnix = std::strtoull(v.c_str(), nullptr, 10);
					haveExpiry = true;
				}
			}
			if (nonce.size() < 8 || nonce.size() > 64) { why = "inject.on names no nonce= of 8-64 letters/digits"; return false; }
			for (char c : nonce)
				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) { why = "the nonce is not letters/digits"; return false; }
			if (!haveExpiry) { why = "inject.on names no expires="; return false; }
			if (expiresUnix <= nowUnix) { why = "the test session in inject.on has expired"; return false; }
			if (expiresUnix - nowUnix > kMaxSessionSeconds) { why = "the test session in inject.on ends more than an hour from now"; return false; }
			return true;
		}

		bool AddLine(const std::string& line, std::string& why)
		{
			if (!SessionLive()) { why = "no test session"; return false; }
			std::lock_guard<std::mutex> g(g_session);
			if (g_sessionCommands >= kMaxSessionCommands) { why = "the session's 2000 commands are used"; return false; }
			ctl::InjectCommand c;
			if (!ctl::parseInject(line, &g_section, c, why)) return false;
			why = g_table.add(c, NowMs());
			if (!why.empty()) return false;
			++g_sessionCommands;
			return true;
		}

		// One inject.txt as read: "nonce=<the session's>" first, then at most 32 commands. Logs each; returns those accepted.
		int TakeFile(const std::string& text, std::string& why)
		{
			why.clear();
			if (text.size() > kMaxFileBytes) { why = "inject.txt is larger than 4096 bytes; nothing read"; return 0; }
			std::istringstream in(text);
			std::string line, nonce;
			{ std::lock_guard<std::mutex> g(g_session); nonce = g_nonce; }
			bool first = true;
			int commands = 0, accepted = 0;
			while (std::getline(in, line))
			{
				const std::string t = ctl::trim(line);
				if (t.empty()) continue;
				if (first)
				{
					first = false;
					if (t != "nonce=" + nonce) { why = "inject.txt does not name this session's nonce; nothing read"; return 0; }
					continue;
				}
				if (++commands > kMaxFileCommands) { why = "inject.txt has more than 32 commands; the rest were not read"; break; }
				std::string w;
				if (AddLine(t, w)) { ++accepted; Log("accepted: " + t); }
				else Log("refused \"" + t + "\": " + w);
			}
			if (first) why = "inject.txt is empty";
			return accepted;
		}

		bool ArmWith(const std::vector<std::string>& controlsLines, std::string& why)
		{
			ctl::Section s = ctl::parseSection(controlsLines);
			if (!s.error.empty()) { why = "the [Controls] profile does not parse: " + s.error; return false; }
			g_table.clearFor("");   // a new arming starts with nothing running
			std::lock_guard<std::mutex> g(g_session);
			g_section = std::move(s);
			g_sessionCommands = 0;
			g_armed = true;
			return true;
		}

		int DeliverView(const std::string& dev, DIJOYSTATE2& state, int buttons, int povs,
			const std::function<bool(int, long&, long&)>& range)
		{
			if (!SessionLive()) return 0;
			ctl::StateView v;
			static_assert(offsetof(DIJOYSTATE2, rglSlider) == offsetof(DIJOYSTATE2, lX) + 6 * sizeof(LONG), "DIJOYSTATE2 axes are not contiguous");
			v.axes = reinterpret_cast<long*>(&state.lX);   // lX lY lZ lRx lRy lRz rglSlider[0] rglSlider[1]
			v.buttons = state.rgbButtons;
			v.buttonCount = buttons < 0 ? 0 : buttons > 128 ? 128 : buttons;
			v.povs = reinterpret_cast<unsigned long*>(state.rgdwPOV);
			v.povCount = povs < 0 ? 0 : povs > 4 ? 4 : povs;
			int n = 0;
			if (g_table.any())
			{
				std::vector<std::string> lines;
				n = g_table.apply(dev, v, NowMs(), range, lines);
				for (auto& l : lines) Log(l);
			}
			// While record.on exists, the read as the game takes it (injections included) goes to macro-<time>.txt.
			static dbce::macro::Recorder recorder("outrun2006");
			recorder.sample(dev, v, NowMs(), range);
			return n;
		}
	}

	std::string InstanceText(const GUID& g)
	{
		char text[40];
		std::snprintf(text, sizeof text, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2, g.Data3,
			g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
		return text;
	}

	std::vector<std::string> Init(const std::filesystem::path& gameDir)
	{
		std::vector<std::string> log;
		g_armed = false;
		const std::wstring on = DbcePath(L"inject.on");
		if (on.empty() || GetFileAttributesW(on.c_str()) == INVALID_FILE_ATTRIBUTES) return log;   // not requested
		auto refuse = [&](const std::string& why) { log.push_back("RemapInject: test injection refused: " + why); return log; };
		// Force is ruled out for the whole process by the signal mute, latched at its start; without it, nothing arms.
		if (!OutRunSignalMute::BlocksOutput())
			return refuse("the process is not signal-muted (start it with DBCE_OUTRUN_SIGNAL_MUTE, which blocks every force output)");
		std::string sessionText;
		{
			std::ifstream f(on, std::ios::binary);
			char buf[513];
			f.read(buf, sizeof buf);
			sessionText.assign(buf, (size_t)f.gcount());
		}
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		const uint64_t nowUnix = ((((uint64_t)ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10000000ull) - 11644473600ull;
		std::string nonce, why;
		uint64_t expiresUnix = 0;
		if (!ParseSession(sessionText, nowUnix, nonce, expiresUnix, why)) return refuse(why);
		const auto userIni = gameDir / "OutRun2006Tweaks.user.ini", mainIni = gameDir / "OutRun2006Tweaks.ini";
		std::vector<std::string> body;
		if (!ProfileControls::ReadProfile(userIni, body)) return refuse("no [Controls] profile request in OutRun2006Tweaks.user.ini");
		const ProfileControls::Plan plan = ProfileControls::PlanLines(body, ProfileControls::ReadCurrent(mainIni, userIni));
		if (!plan.ok) return refuse("the [Controls] profile does not apply: " + plan.error);
		if (ProfileControls::Pending(userIni, plan)) return refuse("the [Controls] profile is not applied yet");
		if (!ArmWith(body, why)) return refuse(why);
		{
			std::lock_guard<std::mutex> g(g_session);
			g_nonce = nonce;
			g_expiresMs = NowMs() + (expiresUnix - nowUnix) * 1000ull;
		}
		g_commands = DbcePath(L"inject.txt");
		// Only commands written after this start count: the file's time as it is now is the baseline.
		WIN32_FILE_ATTRIBUTE_DATA a;
		g_lastWrite = GetFileAttributesExW(g_commands.c_str(), GetFileExInfoStandard, &a) ? a.ftLastWriteTime : FILETIME{};
		log.push_back("RemapInject: test injection ARMED (signal-muted process: no force output): profile '" + plan.profile +
			"' revision " + plan.revision + "; session ends in " + std::to_string(expiresUnix - nowUnix) + " s");
		return log;
	}

	bool Armed() { return g_armed; }

	void Poll()
	{
		if (!SessionLive() || g_testing || g_commands.empty()) return;
		const uint64_t now = NowMs();
		if (now < g_nextPoll) return;
		g_nextPoll = now + 100;
		WIN32_FILE_ATTRIBUTE_DATA a;
		if (!GetFileAttributesExW(g_commands.c_str(), GetFileExInfoStandard, &a)) return;
		if (CompareFileTime(&a.ftLastWriteTime, &g_lastWrite) <= 0) return;
		g_lastWrite = a.ftLastWriteTime;   // each version is read once, whatever its fate
		std::string text;
		if (a.nFileSizeHigh || a.nFileSizeLow > kMaxFileBytes) text.assign(kMaxFileBytes + 1, ' ');   // refused unread
		else
		{
			std::ifstream in(g_commands, std::ios::binary);
			char buf[kMaxFileBytes + 1];
			in.read(buf, sizeof buf);
			text.assign(buf, (size_t)in.gcount());
		}
		std::string why;
		TakeFile(text, why);
		if (!why.empty()) Log(why);
	}

	int Deliver(const GUID& instance, IDirectInputDevice8A* device, DIJOYSTATE2& state)
	{
		if (!device || !g_armed) return 0;
		DIDEVCAPS caps{ sizeof(DIDEVCAPS) };
		if (FAILED(device->GetCapabilities(&caps))) return 0;
		static const DWORD offsets[8] = { DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1) };
		return DeliverView(InstanceText(instance), state, (int)caps.dwButtons, (int)caps.dwPOVs, [device](int axis, long& mn, long& mx) {
			if (axis < 0 || axis > 7) return false;
			DIPROPRANGE r{};
			r.diph.dwSize = sizeof(DIPROPRANGE);
			r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			r.diph.dwObj = offsets[axis];
			r.diph.dwHow = DIPH_BYOFFSET;
			if (FAILED(device->GetProperty(DIPROP_RANGE, &r.diph)) || r.lMax <= r.lMin) return false;   // no such axis
			mn = r.lMin; mx = r.lMax;
			return true;
		});
	}

	namespace { std::atomic<int> g_volumes[3]{}; }

	int NoteVolume(Volume channel, int value)
	{
		if (g_armed) g_volumes[(int)channel].store(value, std::memory_order_relaxed);
		return value;
	}

	std::string ObserveName()
	{
		if (!g_armed) return {};
		std::lock_guard<std::mutex> g(g_session);
		return "Local\\DbceOutRunInjectObserve-" + g_nonce;
	}

	bool Observe(float speed, float steer, int pedal, uint32_t gear, uint32_t gameMode, bool inGameplay)
	{
		if (!g_armed) return false;
		static ObserveData* view = nullptr;
		static bool failed = false;
		if (!view && !failed)
		{
			const std::string name = ObserveName();
			failed = true;
			HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ObserveData), name.c_str());
			const DWORD error = GetLastError();
			if (!map) { Log("observation mapping " + name + " failed (" + std::to_string(error) + ")"); return false; }
			if (error == ERROR_ALREADY_EXISTS)
			{
				CloseHandle(map);
				Log("observation mapping " + name + " already exists; refused, nothing published");
				return false;
			}
			auto* v = static_cast<ObserveData*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ObserveData)));
			if (!v) { Log("observation mapping " + name + " could not be mapped (" + std::to_string(GetLastError()) + ")"); CloseHandle(map); return false; }
			// The mapping lives as long as the process: one per armed run.
			*v = ObserveData{};
			v->version = 2;
			view = v;
			failed = false;
			Log("observation published as " + name);
		}
		if (!view) return false;
		// Sequence: odd while writing, even when whole (see ObserveData).
		auto* seq = reinterpret_cast<volatile LONG*>(&view->packetId);
		InterlockedIncrement(seq);
		MemoryBarrier();
		view->tickMs = GetTickCount();
		view->speed = speed;
		view->steer = steer;
		view->pedal = pedal;
		view->gear = gear;
		view->gameMode = gameMode;
		view->inGameplay = inGameplay ? 1 : 0;
		view->steerVolume = g_volumes[0].load(std::memory_order_relaxed);
		view->accelVolume = g_volumes[1].load(std::memory_order_relaxed);
		view->brakeVolume = g_volumes[2].load(std::memory_order_relaxed);
		MemoryBarrier();
		InterlockedIncrement(seq);
		return true;
	}

	void DeviceGone(const GUID& instance)
	{
		if (!g_table.any()) return;
		const int n = g_table.clearFor(InstanceText(instance));
		if (n) Log(InstanceText(instance) + " read failed or was released; " + std::to_string(n) + " running sample(s) dropped");
	}

	bool TestArm(const std::vector<std::string>& controlsLines)
	{
		g_testing = true;
		g_armed = false;
		std::string why;
		{ std::lock_guard<std::mutex> g(g_session); g_nonce = "test"; g_expiresMs = 0; }
		return ArmWith(controlsLines, why);
	}
	bool TestCommand(const std::string& line, std::string& why) { return AddLine(line, why); }
	int TestFile(const std::string& text, std::string& why) { return TakeFile(text, why); }
	void TestExpire(uint64_t expiresMs) { std::lock_guard<std::mutex> g(g_session); g_expiresMs = expiresMs; }
	bool TestSession(const std::string& text, uint64_t nowUnix, std::string& nonceOrWhy)
	{
		std::string nonce, why;
		uint64_t expires = 0;
		const bool ok = ParseSession(text, nowUnix, nonce, expires, why);
		nonceOrWhy = ok ? nonce : why;
		return ok;
	}
	void TestClock(uint64_t nowMs) { g_testClock = nowMs; }
	int TestDeliver(const GUID& instance, DIJOYSTATE2& state, int buttons, int povs, long axisMin, long axisMax)
	{
		return DeliverView(InstanceText(instance), state, buttons, povs, [axisMin, axisMax](int, long& mn, long& mx) {
			if (axisMax <= axisMin) return false;
			mn = axisMin; mx = axisMax;
			return true;
		});
	}
}
