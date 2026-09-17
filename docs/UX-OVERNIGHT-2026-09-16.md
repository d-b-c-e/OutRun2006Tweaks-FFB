# OutRun wheel settings adoption — overnight 2026-09-16 / 17

Guidance: toolkit commit **a84bebab5ec2abdcd5140b9c63c139ccff86a7d3**,
UX-1 including UX-01-S, UX-04-H, UX-05-D and UX-06-K. Starting source:
`220f660` (`master`, clean). Work branch: `codex/ux-simple-settings-2026-09-16`.
This is a **built partial adoption**, not completed UX reconciliation.

## Implemented

- F6 opens **Wheel settings** independently of the upstream F11 developer
  windows. Six standard pages: Setup, Controls, FFB, Cameras, Telemetry, Help.
  The panel is opaque; page content scrolls beneath a fixed view/navigation/header.
- Simple defaults for absent/invalid `WheelSettings/View`; an explicit choice
  persists. Both views use existing `Settings` values. Switching views does not
  reset a tune, change an input backend, acquire a wheel or enable a stream.
  A hidden custom tune is summarized with **Review in Advanced**.
- Simple has wheel/axis status, direct primary-wheel axis/button Bind with a
  provisional candidate and Save binding/Cancel, inversion/deadzone, grouped
  driving/menu buttons, FFB Off/On/device/Strength/status, native Change camera
  binding, telemetry Off/On/destination/status, Help and UI scale.
- A successful Steering binding saves the actual primary device GUID in the
  same atomic settings write. Capture never changes the old assignment before
  save; cancel, timeout, focus loss, disconnect and failed save retain it.
  Held buttons must be released before Save binding is allowed. Capture disables
  view/navigation/close, and Esc cancels first. Main buttons are one-based.
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
- The production FFB update/selection functions run against fake ABI callbacks:
  six inactive gates zero both effect types before any initialization; switching
  releases before another open; unbound steering follow is refused; an explicit
  override wins over the steering GUID; driver refusal does not try another
  actuator; a zero game HWND is rejected before native initialization.

`tools/Check-IniCoverage.ps1`: **Pass**, 148 parsed settings / 176 template keys,
including the 28 existing documented dead CDTracks entries. `git diff --check`:
**Pass**. Component hashes match the verified official package, and model/profile/
encoder/proxy files remain identical to the v0.8.0 baseline. A routine toolkit
sync is rejected before changes while the native override is active.

These are source/build/offline-fixture results. **No game was launched, no DLL
deployed, no screenshot captured in game, no real device bound, and no physical
torque tested.** Text logs at two viewport sizes do not establish readability,
focus navigation, game coexistence or high-DPI acceptance. Candidate artifacts
are local and not a release. The installed game is unchanged.

## Required remaining work

| Requirement | Status / concrete next work |
|---|---|
| UX-01-S complete first setup in Simple | **Partial**. Enable wheel controls is saved for next launch because input hooks are startup-only. No runtime backend switch. Device chooser/calibration and restart-free onboarding remain work. |
| Guided rest/full/center calibration | **Gap**. Capture currently binds an axis with its existing fixed device range; it does not capture endpoints. Add transactional normalized calibration before claiming first-drive completion. |
| Independent pedals/shifter/button box | **Gap**. Primary-wheel binding works in source. Additional slots remain startup INI settings; separate pedal devices are unsupported. |
| Handbrake axis/button | **Not available in the adapter**. Its known SwitchId/ADChannel mapping has no handbrake action. This is not proof the game cannot support one; investigate the game route before registering a permanent exception. |
| Full ordinary binding set | **Partial**. Primary driving/menu bindings are available. Settings/Stop hotkeys, remaining native actions, keyboard binding and context-aware conflict checks need unification. Existing SDL route still links its legacy bindings modal. |
| UX-05-D device picker | **Implemented, live Not tested**. Requires attended follow/override, reorder, disconnect, duplicate-device and acquisition-refusal walks on the packaged build. |
| FFB defaults / physical feel | **Gap / Not tested**. Fresh remains Off and steering source/scale is unverified. Validate it before changing safe defaults; do not retune by guessing. |
| Bonnet/Bumper/native camera ownership | **Gap**. Mod mounts, native-cycle integration, numpad pose controls, adjustment rebinding and ownership gate have not been implemented. Stock Change camera binding is available. |
| Telemetry destination/recording | **Partial**. Existing fixed receiver path is surfaced. Editable atomic connection settings, presets validated with receivers and bounded recording/support collection remain work. |
| Installer/setup | **Gap**. Still copy the DLL pair; no standard Install.bat/backup/rollback/uninstall flow yet. Never overwrite an owner's existing INIs during manual update. |
| 720p/4K keyboard/mouse UI walk | **Not tested**. Offline ImGui rendering covers content selection, not actual appearance/navigation. F11 upstream tools remain unreconciled. |
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
| DirectInput/DeviceGuid | Simple | Saved by Steering Bind; chooser gap | auto |
| DirectInput/SteeringAxis | Simple | F6 live / saved | 0 / axis index (INI 0-based, UI 1-based) |
| DirectInput/SteeringInvert | Simple | F6 live / saved | false |
| DirectInput/SteeringSensitivity | Advanced | F6 live / saved | 1.0 |
| DirectInput/AccelerationAxis | Simple | F6 live / saved | 1 / axis index (INI 0-based, UI 1-based) |
| DirectInput/AccelerationInvert | Simple | F6 live / saved | false |
| DirectInput/BrakeAxis | Simple | F6 live / saved | 2 / axis index (INI 0-based, UI 1-based) |
| DirectInput/BrakeInvert | Simple | F6 live / saved | false |
| DirectInput/ButtonA | Simple on demand | F6 direct Bind/Clear | 0 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonB | Simple on demand | F6 direct Bind/Clear | 1 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonX | Simple on demand | INI only (unreconciled) | 2 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonY | Simple on demand | INI only (unreconciled) | 3 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonStart | Simple on demand | F6 direct Bind/Clear | 7 / button index (INI 0-based, UI 1-based) |
| DirectInput/ButtonBack | Simple on demand | INI only (unreconciled) | 6 / button index (INI 0-based, UI 1-based) |
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
