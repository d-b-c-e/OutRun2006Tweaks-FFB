# OutRun wheel settings adoption — overnight 2026-09-16 / 17

Guidance: toolkit commit **a84bebab5ec2abdcd5140b9c63c139ccff86a7d3**,
UX-1 including UX-01-S, UX-04-H, UX-05-D and UX-06-K. Starting source:
`220f660` (`master`, clean). Work branch: `codex/ux-simple-settings-2026-09-16`.
This is a **built partial adoption**, not completed UX reconciliation.
Visual reference: toolkit `95cbd89`, `docs/reference/wheel-settings.html`.
Resumed 2026-09-19 with owner authorization to build/package/deploy locally.

## Implemented

- F6 opens **Wheel settings** independently of the upstream F11 developer
  windows. Six standard pages: Setup, Controls, FFB, Cameras, Telemetry, Help.
  The panel is opaque; page content scrolls beneath a fixed view/navigation/header.
- Simple defaults for absent/invalid `WheelSettings/View`; an explicit choice
  persists. Both views use existing `Settings` values. Switching views does not
  reset a tune, change an input backend, acquire a wheel or enable a stream.
  A hidden custom tune is summarized with **Review in Advanced**.
- Simple has wheel/axis status, direct primary-wheel axis/button Bind with a
  provisional candidate and Save/Cancel, guided axis calibration, grouped
  driving/menu buttons, FFB Off/On/device/Strength/status, native Change camera
  binding, telemetry Off/On/destination/status, Help and UI scale.
- A successful axis calibration saves its actual device GUID in the
  same atomic settings write. Capture never changes the old assignment before
  save; cancel, timeout, focus loss, disconnect and failed save retain it.
  Held buttons must be released before Save binding is allowed. Capture disables
  view/navigation/close, and Esc cancels first. Main buttons are one-based.
- Bind/Calibrate captures wheel center or pedal rest, full travel, and a device
  preview with inversion/deadzone. Multiple moving axes are rejected. Finite,
  ordered endpoints with minimum usable travel are required. No axis/identity/
  endpoint changes become effective until the atomic Save calibration succeeds.
  Cancellation or a failed save cannot be committed by a later Stop/view edit.
  Legacy transforms remain unchanged until the explicit calibrated flag is saved.
- Throttle and Brake Bind each offer a direct input-device dropdown, including
  separate USB pedals. GUID/name, axis and calibration save as one transaction;
  Steering identity and FFB override are untouched. Identical pedal GUIDs share
  one input handle on the game's existing DirectInput instance. Missing/refused
  identities never fall back; disconnected explicit pedal input is neutral.
- Steering Bind uses the same device picker, including recovery from a missing
  primary wheel. Save zeroes/releases FFB before adopting the captured input
  handle. Pedals that followed the old primary are pinned to that old identity
  in the same transaction, preserving their endpoints; explicit overrides remain.
- F6 uses a scoped Segoe UI font (system font, not redistributed), neutral/cyan
  palette, left navigation and fixed top controls. The upstream F11 font/style
  remains separate. Long device names wrap; scale is adjustable from Help.
- Advanced adds legacy force gains, noise floor, invert force, diagnostic log,
  sensitivity, raw axes, telemetry signal caveats and local paths. Startup-only
  settings are labeled read-only. Dormant wheel-torque calibration is not offered
  as a working setting. Named-profile tuning stays in the existing profile file.
- Simple FFB has a real device dropdown and Refresh. Default **Use steering
  wheel** resolves the saved Steering identity; explicit GUID/name overrides are
  retained independently. Missing/unbound/refused selections cannot choose a
  different actuator. Old index-only selections require a deliberate replacement.
  Duplicate device names show a short identity suffix in the list.
- The selected actuator is zeroed/released before a target change. Panel, binding
  dialog, focus and non-driving game-state gates zero constant and periodic output
  before initialization. Existing force computation is unchanged. The native
  component pin and why it changed are documented in [the pin review](NATIVE-PIN-2026-09-17.md).
- Stop FFB and F8 save Off; On is the single resume preference. Stop is fixed on
  every page. Existing Off and the old new-configuration Off default remain:
  **the unverified steering signal is an open UX-05 validation/default gap**,
  not a game-imposed exception or a reason to claim compliance.
- Telemetry status uses actual UDP send results, says receiver unconfirmed, and
  Off stops/tears down the stream. The existing fixed destination remains
  `127.0.0.1:8000`; no output protocol or packet interpretation changed.
- Edited keys merge into the existing user override while preserving unknown
  data, comments and line endings; temp-write/atomic replace keeps the old file
  on failure. The first edit backs up the previous override. Save failure remains
  visible across pages, with retry for committed edits. Sliders defer disk writes
  until release; view/close/F6 flush pending edits without resetting values.
- Packaged Install.bat recognizes existing OutRun2006Tweaks, records its version
  and hash, backs up runtime/settings, verifies package and installed hashes,
  and retains all existing INIs/bindings/profiles. Restore returns the previous
  runtime and retains settings. Unknown proxies require deliberate inspection.

## Verification and its limits

`cmake --build build --config Release --target outrun2006tweaks`: **Pass**, x86
Release; output `build/bin/dinput8.dll`, pinned `WheelFfb.dll`, profile INI.

`tools/tests/Test-WheelSettings.ps1`: **Pass**, using the real ImGui renderer
without a native window, game, DirectInput acquisition or real output:

- Upgrade fixtures retain custom force tune, saved Off, unknown sections,
  comments and backup; missing/invalid view is Simple. Locked-file replace fails
  without losing the old file, then retry succeeds.
- All six pages in Simple/Advanced at 1280x720 and 3840x2160 execute; text logs
  prove core controls/stop remain and detailed FFB gains are absent in Simple.
  A view round trip preserves the same tune and Off/telemetry state.
- Provisional button capture detects a candidate without changing the old
  binding; disconnect cancels it. Stop persists Off.
- Guided calibration verifies atomic save/identity, invalid endpoint rejection,
  locked-file failure, disconnect/cancel and no delayed partial commit. The actual
  input-reader fixture covers 144 legacy opt-out cases plus centered, released,
  full, partial, inverted, clamped, cleared and invalid calibrated input.
- Successor fixtures additionally exercise independent/shared pedal identities,
  missing/refused selection, neutral disconnect and same-identity recovery,
  primary-handle adoption, preservation of old pedal sources and zero-before-swap.
  Real INIReader regression covers old upstream files without an FFB section;
  the first live attempt exposed and rolled back that migration defect (receipt).
- The production FFB update/selection functions run against fake ABI callbacks:
  six inactive gates zero both effect types before any initialization; switching
  releases before another open; unbound steering follow is refused; an explicit
  override wins over the steering GUID; driver refusal does not try another
  actuator; a zero game HWND is rejected before native initialization.

`tools/Check-IniCoverage.ps1`: **Pass**, 166 parsed settings / 194 template keys,
including the 28 existing documented dead CDTracks entries. `git diff --check`:
**Pass**. Component hashes match the verified official package, and model/profile/
encoder/proxy files remain identical to the v0.8.0 baseline. A routine toolkit
sync is rejected before changes while the native override is active.

`tools/tests/Test-WheelInstall.ps1`: **Pass**, synthetic x86 executable; package
validation, unknown-proxy refusal, existing config/binding retention, seeded
defaults, backup, restore, locked-second-DLL rollback and tamper refusal.

Production ImGui draw data and the actual font atlas were rasterized offline:
`build/wheel-settings-fixture/run-1de7275af5234449ada49abd835d1c2f` contains
29 frames, text logs and `render-manifest.json` with per-frame viewport, scale,
window bounds, content extent and scroll maxima. The coordinator reviewed FFB
at 720p/4K; calibration and long-device frames were inspected locally. Bounds
checks keep the fixed window/navigation inside the viewport. These demonstrate
renderer-level layout, not game/GPU/high-DPI or keyboard-navigation acceptance.

Successor input-device/parser evidence:
`build/wheel-settings-fixture/run-500e6b16b92f4cf0b130604166e18925` has
30 production frames including `1280-separate-pedal-device-simple.png`.
The direct device selector, rest action and Cancel fit together at 720p.
The x86 build and complete UI/input/fake-ABI suite pass for this successor.

**No game was launched, no screenshot captured in game, no real device bound,
and no physical torque tested.** Deployment is recorded separately with exact
package/installed hashes and retention checks. Candidate artifacts are local,
not a public release.

## Required remaining work

| Requirement | Status / concrete next work |
|---|---|
| UX-01-S complete first setup in Simple | **Implemented controls path, live Not tested**. Device choice, calibration and recovery are in Simple. Initial input-route enable takes one normal restart because hooks/backends are selected at startup; there is no runtime backend switch. |
| Guided rest/full/center calibration | **Implemented for primary device, live Not tested**. Transactional identity/axis/endpoints, ambiguity rejection, inversion/deadzone and finite range checks pass offline. |
| Independent pedals/shifter/button box | **Pedals implemented, live Not tested**. Per-pedal GUID/axis/calibration transaction and shared USB handles pass source fixtures. Shifter/button-box slots remain startup INI settings. |
| Handbrake axis/button | **Not available in the adapter**. Its known SwitchId/ADChannel mapping has no handbrake action. This is not proof the game cannot support one; investigate the game route before registering a permanent exception. |
| Full ordinary binding set | **Partial**. Primary driving/menu bindings are available. Settings/Stop hotkeys, remaining native actions, keyboard binding and context-aware conflict checks need unification. Existing SDL route still links its legacy bindings modal. |
| UX-05-D device picker | **Implemented, live Not tested**. Requires attended follow/override, reorder, disconnect, duplicate-device and acquisition-refusal walks on the packaged build. |
| FFB defaults / physical feel | **Gap / Not tested**. Fresh remains Off and steering source/scale is unverified. Validate it before changing safe defaults; do not retune by guessing. |
| Bonnet/Bumper/native camera ownership | **Gap**. Mod mounts, native-cycle integration, numpad pose controls, adjustment rebinding and ownership gate have not been implemented. Stock Change camera binding is available. |
| Telemetry destination/recording | **Partial**. Existing fixed receiver path is surfaced. Editable atomic connection settings, presets validated with receivers and bounded recording/support collection remain work. |
| Installer/setup | **Implemented and fixture-tested**. Install.bat, recognized proxy/version/hash, timestamped backup, config retention, runtime restore and failure rollback. See deployment receipt for installed evidence. |
| 720p/4K keyboard/mouse UI walk | **Layout partially verified**. Production draw-data PNGs cover visual appearance/bounds, not live input/focus/DPI. F11 upstream tools remain unreconciled. |
| Lifecycle while a car is moving | **Not tested**. Existing overlay suspends game ReadIO; verify neutralization/held-input release and pause behavior during capture and close. |

## Placement inventory

The following inventory includes every shipped key. “INI only” is remaining
implementation work, not an Advanced UI completion claim. Actions not backed by
an INI key: Close/Stop/Refresh and errors are Simple; axis/button capture is Simple
on demand; raw device diagnostics/profile editing details are Advanced. All Simple
items remain available in Advanced. Source: `src/overlay/wheel_settings.cpp`,
`src/dllmain.cpp`, `src/hooks_inputremap.cpp`, `src/hooks_dinputffb.cpp`.

<!-- OPTION-INVENTORY -->
| Saved key | Intended placement | Current access | Shipped default / units |
|---|---|---|---|
| Performance/FramerateLimit | Advanced | INI only (unreconciled) | 60 |
| Performance/FramerateLimitMode | Advanced | INI only (unreconciled) | 0 |
| Performance/FramerateFastLoad | Advanced | INI only (unreconciled) | 3 |
| Performance/FramerateUnlockExperimental | Advanced | INI only (unreconciled) | true |
| Performance/VSync | Advanced | INI only (unreconciled) | 1 |
| Performance/SingleCoreAffinity | Advanced | INI only (unreconciled) | true |
| Controls/UseNewInput | Simple on demand | Setup saves wheel route for next launch | false |
| Controls/SteeringDeadZone | Simple | F6 live / saved | 0.2 / ratio (UI %) |
| Controls/ControllerHotPlug | Advanced | INI only (unreconciled) | false |
| Controls/DefaultManualTransmission | Simple on demand | INI only (unreconciled) | false |
| Controls/HudToggleKey | Advanced | INI only (unreconciled) | empty |
| Controls/VibrationMode | Advanced | INI only (unreconciled) | 0 |
| Controls/VibrationControllerId | Advanced | INI only (unreconciled) | 0 |
| Controls/VibrationStrength | Advanced | INI only (unreconciled) | 7 / ratio (UI %) |
| Controls/ImpulseVibrationMode | Advanced | INI only (unreconciled) | 2 |
| Controls/ImpulseVibrationLeftMultiplier | Advanced | INI only (unreconciled) | 0.20 |
| Controls/ImpulseVibrationRightMultiplier | Advanced | INI only (unreconciled) | 0.20 |
| DirectInput/UseDirectInputRemap | Simple on demand | Setup saves wheel route for next launch | false |
| DirectInput/DeviceGuid | Simple | Steering Bind device dropdown / atomic Save | auto |
| DirectInput/SteeringAxis | Simple | F6 live / saved | 0 / axis index (INI 0-based, UI 1-based) |
| DirectInput/SteeringInvert | Simple | F6 live / saved | false |
| DirectInput/SteeringSensitivity | Advanced | F6 live / saved | 1.0 |
| DirectInput/AccelerationAxis | Simple | F6 live / saved | 1 / axis index (INI 0-based, UI 1-based) |
| DirectInput/AccelerationInvert | Simple | F6 live / saved | false |
| DirectInput/BrakeAxis | Simple | F6 live / saved | 2 / axis index (INI 0-based, UI 1-based) |
| DirectInput/BrakeInvert | Simple | F6 live / saved | false |
| DirectInput/AccelerationDeadzone | Simple on demand | F6 calibration preview / atomic Save | 0 / ratio (UI %) |
| DirectInput/BrakeDeadzone | Simple on demand | F6 calibration preview / atomic Save | 0 / ratio (UI %) |
| DirectInput/ThrottleDeviceGuid | Simple on demand | F6 Throttle Bind device dropdown / atomic Save | empty = primary wheel |
| DirectInput/ThrottleDeviceName | Simple on demand | Saved friendly name with device identity | empty |
| DirectInput/BrakeDeviceGuid | Simple on demand | F6 Brake Bind device dropdown / atomic Save | empty = primary wheel |
| DirectInput/BrakeDeviceName | Simple on demand | Saved friendly name with device identity | empty |
| DirectInput.Calibration/SteeringEnabled | Simple on demand | F6 Bind/Calibrate / atomic Save | false |
| DirectInput.Calibration/SteeringMinimum | Simple on demand | Captured endpoint / atomic Save | 0 / normalized device units |
| DirectInput.Calibration/SteeringCenter | Simple on demand | Captured center / atomic Save | 32767.5 / normalized device units |
| DirectInput.Calibration/SteeringMaximum | Simple on demand | Captured endpoint / atomic Save | 65535 / normalized device units |
| DirectInput.Calibration/ThrottleEnabled | Simple on demand | F6 Bind/Calibrate / atomic Save | false |
| DirectInput.Calibration/ThrottleMinimum | Simple on demand | Captured endpoint / atomic Save | 0 / normalized device units |
| DirectInput.Calibration/ThrottleCenter | Simple on demand | Derived midpoint / atomic Save | 32767.5 / normalized device units |
| DirectInput.Calibration/ThrottleMaximum | Simple on demand | Captured endpoint / atomic Save | 65535 / normalized device units |
| DirectInput.Calibration/BrakeEnabled | Simple on demand | F6 Bind/Calibrate / atomic Save | false |
| DirectInput.Calibration/BrakeMinimum | Simple on demand | Captured endpoint / atomic Save | 0 / normalized device units |
| DirectInput.Calibration/BrakeCenter | Simple on demand | Derived midpoint / atomic Save | 32767.5 / normalized device units |
| DirectInput.Calibration/BrakeMaximum | Simple on demand | Captured endpoint / atomic Save | 65535 / normalized device units |
| DirectInput/ButtonA | Simple on demand | F6 direct Bind/Clear | 0 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonB | Simple on demand | F6 direct Bind/Clear | 1 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonX | Simple on demand | F6 direct Bind/Clear | 2 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonY | Simple on demand | F6 direct Bind/Clear | 3 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonStart | Simple on demand | F6 direct Bind/Clear | 7 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonBack | Simple on demand | F6 direct Bind/Clear | 6 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonGearUp | Simple on demand | F6 direct Bind/Clear | 4 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonGearDown | Simple on demand | F6 direct Bind/Clear | 5 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonChangeView | Simple on demand | F6 direct Bind/Clear | 8 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonSelUp | Simple on demand | F6 direct Bind/Clear | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonSelDown | Simple on demand | F6 direct Bind/Clear | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonSelLeft | Simple on demand | F6 direct Bind/Clear | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonSelRight | Simple on demand | F6 direct Bind/Clear | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/DeviceGuid | Simple on demand | INI only (unreconciled) | empty |
| DirectInput.Shifter/GearMode | Simple on demand | INI only (unreconciled) | sequential |
| DirectInput.Shifter/ButtonGearUp | Simple on demand | INI only (unreconciled) | 4 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGearDown | Simple on demand | INI only (unreconciled) | 5 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear1 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear2 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear3 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear4 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear5 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGear6 | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Shifter/ButtonGearReverse | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/DeviceGuid | Simple on demand | INI only (unreconciled) | empty |
| DirectInput.Aux/ButtonA | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonB | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonX | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonY | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonStart | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonBack | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonGearUp | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonGearDown | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonChangeView | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonSelUp | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonSelDown | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonSelLeft | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| DirectInput.Aux/ButtonSelRight | Simple on demand | INI only (unreconciled) | -1 / button index (INI 0-based, UI 1-based) |
| Telemetry/Enable | Simple | F6 live / saved | false |
| Telemetry/SharedMemName | Advanced | F6 read-only; edit INI then restart | OutRun2006Telemetry |
| FFB/DirectInputFFB | Simple | F6 live / saved | false |
| FFB/FFBDevice | Advanced | Legacy index; replace explicitly with dropdown | -1 |
| FFB/FFBDeviceGuid | Simple | F6 live / saved | steering |
| FFB/FFBDeviceName | Simple | F6 live / saved | empty |
| FFB/FFBProfile | Advanced | F6 read-only; edit INI then restart | legacy |
| FFB/FFBGlobalStrength | Simple | F6 live / saved | 1.0 / ratio (UI %) |
| FFB/FFBSpringStrength | Advanced | F6 live / saved | 0.45 / ratio (UI %) |
| FFB/FFBDamperStrength | Advanced | F6 live / saved | 0.10 / ratio (UI %) |
| FFB/FFBSteeringWeight | Advanced | F6 live / saved | 0.55 |
| FFB/FFBGripLoss | Advanced | F6 live / saved | 0.6 / ratio (UI %) |
| FFB/FFBWeightTransfer | Advanced | F6 live / saved | 0.8 / ratio (UI %) |
| FFB/FFBLateralDeadzone | Advanced | F6 live / saved | 1.5 |
| FFB/FFBWallImpact | Advanced | F6 live / saved | 1.0 / ratio (UI %) |
| FFB/FFBGearShift | Advanced | F6 live / saved | 0.3 / ratio (UI %) |
| FFB/FFBRoadTexture | Advanced | F6 live / saved | 0.6 / ratio (UI %) |
| FFB/FFBTireSlip | Advanced | F6 live / saved | 0.35 / ratio (UI %) |
| FFB/FFBRumbleStrip | Advanced | Parsed but not applied by current force adapter | 0.35 |
| FFB/FFBEngineIdle | Advanced | F6 live / saved | 0.08 / ratio (UI %) |
| FFB/FFBUsePeriodicEffects | Advanced | F6 read-only; edit INI then restart | true |
| FFB/FFBWheelTorqueNm | Advanced | Parsed but not applied by current force adapter | 0.0 / Nm |
| FFB/FFBInvertForce | Advanced | F6 live / saved | false |
| FFB/FFBDiagnosticLog | Advanced | F6 live / saved | false |
| Graphics/UIScalingMode | Advanced | INI only (unreconciled) | 1 |
| Graphics/UILetterboxing | Advanced | INI only (unreconciled) | 1 |
| Graphics/AnisotropicFiltering | Advanced | INI only (unreconciled) | 16 |
| Graphics/ReflectionResolution | Advanced | INI only (unreconciled) | 2048 |
| Graphics/UseHiDefCharacters | Advanced | INI only (unreconciled) | true |
| Graphics/TransparencySupersampling | Advanced | INI only (unreconciled) | true |
| Graphics/ScreenEdgeCullFix | Advanced | INI only (unreconciled) | true |
| Graphics/DisableVehicleLODs | Advanced | INI only (unreconciled) | true |
| Graphics/DisableStageCulling | Advanced | INI only (unreconciled) | true |
| Graphics/FixZBufferPrecision | Advanced | INI only (unreconciled) | true |
| Graphics/CarBaseShadowOpacity | Advanced | INI only (unreconciled) | 1.0 |
| Graphics/DrawDistanceIncrease | Advanced | INI only (unreconciled) | 0 |
| Graphics/DrawDistanceBehind | Advanced | INI only (unreconciled) | 0 |
| Graphics/TextureBaseFolder | Advanced | INI only (unreconciled) | textures |
| Graphics/SceneTextureReplacement | Advanced | INI only (unreconciled) | true |
| Graphics/UITextureReplacement | Advanced | INI only (unreconciled) | true |
| Graphics/SceneTextureExtract | Advanced | INI only (unreconciled) | false |
| Graphics/UITextureExtract | Advanced | INI only (unreconciled) | false |
| Graphics/EnableTextureCache | Advanced | INI only (unreconciled) | true |
| Graphics/UseNewTextureAllocator | Advanced | INI only (unreconciled) | true |
| Audio/AllowHorn | Advanced | INI only (unreconciled) | true |
| Audio/AllowWAV | Advanced | INI only (unreconciled) | true |
| Audio/AllowFLAC | Advanced | INI only (unreconciled) | true |
| CDSwitcher/SwitcherEnable | Advanced | INI only (unreconciled) | false |
| CDSwitcher/SwitcherDisplayTitle | Advanced | INI only (unreconciled) | true |
| CDSwitcher/SwitcherTitleFont | Advanced | INI only (unreconciled) | 2 |
| CDSwitcher/SwitcherTitleFontSizeX | Advanced | INI only (unreconciled) | 0.3 |
| CDSwitcher/SwitcherTitleFontSizeY | Advanced | INI only (unreconciled) | 0.5 |
| CDSwitcher/SwitcherTitlePositionX | Advanced | INI only (unreconciled) | 375 |
| CDSwitcher/SwitcherTitlePositionY | Advanced | INI only (unreconciled) | 450 |
| CDSwitcher/SwitcherShuffleTracks | Advanced | INI only (unreconciled) | false |
| CDSwitcher/TrackNext | Advanced | INI only (unreconciled) | Back |
| CDSwitcher/TrackPrevious | Advanced | INI only (unreconciled) | RS+Back |
| Window/WindowedBorderless | Advanced | INI only (unreconciled) | true |
| Window/WindowPositionX | Advanced | INI only (unreconciled) | 0 |
| Window/WindowPositionY | Advanced | INI only (unreconciled) | 0 |
| Window/WindowedHideMouseCursor | Advanced | INI only (unreconciled) | true |
| Window/DisableDPIScaling | Advanced | INI only (unreconciled) | true |
| Window/AutoDetectResolution | Advanced | INI only (unreconciled) | true |
| Misc/EnableHollyCourse2 | Advanced | INI only (unreconciled) | 3 |
| Misc/SkipIntroLogos | Advanced | INI only (unreconciled) | false |
| Misc/DisableCountdownTimer | Advanced | INI only (unreconciled) | false |
| Misc/EnableLevelSelect | Advanced | INI only (unreconciled) | false |
| Misc/RestoreJPClarissa | Advanced | INI only (unreconciled) | false |
| Misc/ShowOutRunMilesOnMenu | Advanced | INI only (unreconciled) | true |
| Misc/AllowCharacterSelection | Advanced | INI only (unreconciled) | false |
| Misc/RandomHighwayAnimSets | Advanced | INI only (unreconciled) | false |
| Misc/DemonwareServerOverride | Advanced | INI only (unreconciled) | clarissa.port0.org |
| Misc/ProtectLoginData | Advanced | INI only (unreconciled) | true |
| Overlay/Enabled | Advanced | INI only (unreconciled) | true |
| WheelSettings/View | Simple | F6 live / saved | Simple |
| Bugfixes/FixPegasusClopping | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixRightSideBunkiAnimations | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixC2CRankings | Advanced | INI only (unreconciled) | true |
| Bugfixes/PreventDESTSaveCorruption | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixLensFlarePath | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixIncorrectShading | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixParticleRendering | Advanced | INI only (unreconciled) | true |
| Bugfixes/FixFullPedalChecks | Advanced | INI only (unreconciled) | true |
| Bugfixes/HideOnlineSigninText | Advanced | INI only (unreconciled) | false |
| CDTracks/01_Splash_wave.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Splash Wave |
| CDTracks/02_Magical_Sound_Shower.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Magical Sound Shower |
| CDTracks/03_Passing_Breeze.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Passing Breeze |
| CDTracks/04_Risky_Ride.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Risky Ride |
| CDTracks/05_Shiny_World.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Shiny World |
| CDTracks/06_Night_Flight.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Night Flight |
| CDTracks/07_Life_was_a_Bore.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Life was a Bore |
| CDTracks/08_Radiation.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Radiation |
| CDTracks/09_Night_Bird.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Night Bird |
| CDTracks/10_Splash_wave_1986.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Splash Wave (1986) |
| CDTracks/11_Magical_Sound_Shower_1986.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Magical Sound Shower (1986) |
| CDTracks/12_Passing_Breeze_1986.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Passing Breeze (1986) |
| CDTracks/13_Shake_the_Street_1989.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Shake the Street (1989) |
| CDTracks/14_Rush_a_Difficulty_1989.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Rush a Difficulty (1989) |
| CDTracks/15_Who_are_You_1989.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Who are You (1989) |
| CDTracks/16_Keep_Your_Heart_1989.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Keep Your Heart (1989) |
| CDTracks/17_Splash_Wave_EuroMix.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Splash Wave (EuroMix) |
| CDTracks/18_Magical_Sound_Shower_EuroMix.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Magical Sound Shower (EuroMix) |
| CDTracks/19_Passing_Breeze_EuroMix.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Passing Breeze (EuroMix) |
| CDTracks/20_Risky_Ride_Guitar_Mix.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Risky Ride (Guitar Mix) |
| CDTracks/21_Shake_the_Street_ARRANGED.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Shake the Street (ARRANGED) |
| CDTracks/22_Who_are_you_ARRANGED.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Who are You (ARRANGED) |
| CDTracks/23_Rush_a_difficulty_ARRANGED.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Rush a Difficulty (ARRANGED) |
| CDTracks/24_Keep_your_Heart_ ARRANGED.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Keep your Heart (ARRANGED) |
| CDTracks/25_Shiny_World_prototype.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Shiny World (Prototype) |
| CDTracks/26_Night_Flight_prototype.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Night Flight (Prototype) |
| CDTracks/27_Life_was_a_Bore_Instrumental.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Life was a Bore (Instrumental) |
| CDTracks/28_Night_Flight_Instrumental.ogg | Advanced | Known-dead upstream keys; see CLAUDE.md | Night Flight (Instrumental) |

## Stage 3 disconnect regression — 2026-09-19

Review caught calibrated pedals resolving to the primary wheel bypassing the
new disconnect guard. Current and previous-frame pedal reads now share one
reader, and calibrated primary pedals fail neutral with unavailable telemetry.
The opt-out legacy uncalibrated primary path is unchanged. The source-linked
fixture covers both empty/follow-primary and explicit primary GUIDs after
adoption, stale nonzero current/previous values, disconnect and reconnect.
Release x86 build and all three offline fixture executables pass; fixture run
`run-3055c749f7b14cb1a9e695fb1cdaa64d`. No device acquisition or force output.
