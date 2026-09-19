# Local deployments — 2026-09-19

## Current installation: stage 6

Installed **21:00:23 UTC**, clean/pushed source
`d3d7f2361813b393f137fbdb3be17f4fae825d40`. Native text-entry isolation and
read-only Advanced input diagnostics passed independent source review, x86
Release build and all four offline fixtures. Actual ImGui Help frames were
inspected at 1280x720 and 3840x2160 in
`build/wheel-settings-fixture/run-ec5dcfbecda145aaa31521c658a25469`.

Package `build/packages/wheel-settings-20260919-stage6.zip`, SHA256
`558834f7e82c843b923971698b6ca8d737a652b5b526984dcbd493095136fdcc`.
Installed proxy SHA256
`61b701fd7899aad693fdc37a7d5d2a873967c4747d236c26529d9ea5b34487df`;
native WheelFfb remains
`95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31`.
Both match the package. Backup:
`.wheel-settings-backups/20260919-210023-261-717cb8e2/receipt.json`.
All five owner settings remain byte-identical. Game was closed throughout
deployment. Exact receipt: `build/deployment-stage6-result.json`.
The controlled native-title comparison ran **21:10–21:13 UTC** and reproduced
the same Return failure on both stage 6 and the exact saved pre-session proxy.
The candidate received two Return messages and recorded zero release-blocked
queries. Its actual 3840x2160 Advanced Help was readable; source/UI evidence
does not establish native input, player cameras or licence-textbox acceptance.
Both startup logs verified vibration zeros; WheelFfb.dll was quarantined outside
the game for both input-only runs. Both exited normally, and restoration of
both stage-6 runtime hashes, five original settings, absent test files and empty
SaveGame completed at **21:13:42 UTC** before early lease release.
Evidence: `build/camera-live-stage6/restore-verification.json` and
`candidate-help-diagnostics-3840.png`. See [the comparison result and its
limits](NATIVE-INPUT-DIAGNOSIS-2026-09-19.md). No force or camera acceptance is
implied by this installation.

## Previous installation: stage 5

Installed **20:29:05 UTC**, clean/pushed source
`3d0b51dc14b3e585854e399a426c407ff36eb478`. Steering identity changes now
preflight all retained primary button assignments, including the first
assignment from an empty primary identity. Conflicts are refused before any
INI, pending-write, live identity/axis or force-stop mutation. Coordinator review
closed both replacement and first-assignment cases; the x86 build and all four
production fixtures pass (`build/ux-stage5-build.log`, `build/ux-stage5-fixtures.log`).

Package `build/packages/wheel-settings-20260919-stage5.zip`, SHA256
`016fdc8c2068b162b9e35b6a5e586b79e6e25c08c609abc92932ea7049c9408c`.
Installed proxy SHA256
`58fe0efeecc80b88c8a8b5177fd1892a3a98cb4396b36f22dc9f253d5751a600`;
native WheelFfb remains
`95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31`.
Both match the package. Backup:
`.wheel-settings-backups/20260919-202905-720-26555345/receipt.json`.
All five owner settings remain byte-identical, temporary user/layout overrides
are absent and SaveGame remains empty. Game was closed throughout deployment.
Exact receipt: `build/deployment-stage5-result.json`.

Serialized zero-output smoke ran **20:36–20:41 UTC** from the game folder.
Actual **3840x2160** screenshots show the readable default 2200x1520 panel,
fixed Stop/Close/navigation, Setup/FFB/Controls and Off status. Scrollbar-track
navigation revealed Settings shortcuts; Bind directly captured F9, Esc cancelled,
and the original F6 still closed the panel. The temporary user override remained
byte-identical after cancellation (SHA256
`100845ce522b424b126103e4894247ee1510d44a262dd7c7107594f152ef718a`).

Native Return was tested twice at the title **before the first F6**, without
advancing to a player menu. A/Space/F2 also did not establish that menu. There
are no runtime dispatch counters in this build, so neither the cause nor live
input-isolation acceptance is established. No licence was created; attract-movie
shots are not player-camera evidence. Stock framing remains unidentified, and
no camera hook was added on that basis. No physical controls or nonzero force
are accepted.

Alt+F4 exited normally. Restoration finished **20:41:14 UTC** and the coordinator
lease was released early. All five original configuration hashes and both
runtime hashes match; test-only user/layout files are absent and SaveGame remains
empty. Logs and generated test files were archived. Evidence under
`build/camera-live-stage5/`: `f6-setup-3840.png`, `f6-ffb-off-3840.png`,
`f6-controls-3840.png`, `shortcut-candidate-f9-3840.png`, and
`restore-verification.json`.

## Stage 4: superseded by stage 5

Installed **20:13:22 UTC** from clean/pushed source `72730f51cf270672ebe46e16fd3793562486b21e`.
This adds Simple shifter/button-box capture, function-key Settings/Stop bindings,
transactional Clear, viewport scaling, and final native/SDL/remap input dispatch
isolation with held-menu release gates. Independent review findings were closed
before freeze. The x86 build and all four offline production fixtures pass.

Package `build/packages/wheel-settings-20260919-stage4.zip`, SHA256
`8c36c259e6e2b1426e3b2883abbc3d416394f647f50c0ede163819e7584532be`. Installed proxy SHA256
`03e88ff628649ab72bde47f7cc3cfbb1efad44fc743a02ce6e22b6ca96b7b87d`;
WheelFfb remains `95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31`.
Both installed DLLs match the package. Backup:
`.wheel-settings-backups/20260919-201322-358-4c7ea01f/receipt.json`.
Game was closed; the installer did not launch it or enable FFB.

All five original owner configuration files remain byte-identical. The installer
initially retained six files because stage 3's live smoke had created a separate
`imgui.ini` (created 19:37:49 UTC, hash
`605c380e2290e8ef648b8147ab5a8b8c804543ff97e88c292d70dbe1f121ed3f`).
The original stage-3 restoration retained the original five correctly but missed
this added layout. After verifying it matched the archived smoke output, that
known test-only file was removed with the game closed, restoring its original
absence. `OutRun2006Tweaks.imgui.ini` is a distinct original file and was retained.
Evidence: `build/deployment-stage4-result.json` and
`build/camera-live-stage3/additional-ui-layout-restoration.json`.
Temporary user override is absent; SaveGame is still empty.

Latest offline UI evidence is
`build/wheel-settings-fixture/run-8f8b11a22b544d0594f04860d88521d1`
(33 frames and actual font-atlas raster output). Reviewer checked the default
4K result, but **stage-4 live startup, input isolation and camera identification
remain pending**. No physical controls or nonzero force accepted.

## Stage 3: superseded by stage 4

Installed **19:29:38 UTC**, clean source
`8ac4d9f026644264852d93f8a7435e4d5177ee24` (pushed). Calibrated primary-device
pedals now fail neutral on disconnect in both current and previous-frame
readers; telemetry reports unavailable. All three offline fixture executables
and the x86 Release build pass. The actual-device path remains untested.

Package `build/packages/wheel-settings-20260919-stage3.zip`, SHA256
`8383b567c66cb359ad6f35a0ce30d0df692c9934a58e8202998e5699965ee0f4`.
Installed proxy SHA256
`b36a3cc1e55ff7fbd3a3c28c0981fd6d174ea7ca122f34e8da28d8feed5be67c`;
WheelFfb remains `95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31`.
Both installed runtime hashes match the package. All five existing
configuration files remain unchanged. No process was running during install.
Backup: `.wheel-settings-backups/20260919-192938-828-68521837/receipt.json`.
Zero-output live smoke: normal startup/title/attract and F6/pages work when
launched from the game folder. Direct launch with a different working directory
showed blank frames; it did not establish a runtime failure. The actual 3840x2160
panel was too small, driving the successor viewport-scale change. Player-camera
framing was not identified; no licence was created or wheel binding changed.
FFB was explicitly Off throughout. Alt+F4 exited normally. All five owner settings
were restored exactly; the test-only override was removed, SaveGame remained
empty and runtime hashes were unchanged. Evidence:
`build/camera-live-stage3/f6-correct-folder.png` and
`build/camera-live-stage3/restore-verification.json` (19:44:13 UTC).
The serialized window overran during context recovery; desktop was released
explicitly after restoration. No nonzero physical output was enabled.

## Stage 2: superseded by stage 3

Installed **19:20:56 UTC** from clean source
`310b36dea0e6193da2e66a0eed241b5834ea0382`, pushed on the same scoped branch.
This successor fixes the older-INI startup exception and adds per-device
Steering/Throttle/Brake selection with transactional calibration. Pedals retain
their old source when the primary wheel is replaced; FFB is zeroed before swap.

Package: `build/packages/wheel-settings-20260919-stage2.zip`, SHA256
`a80524364bae4c8fb8208a89a88cd912f1353100cdcdfecff2b75e81a8e9f10c`.
Installed `dinput8.dll` SHA256:
`a2f68db457a82adc9e744f473f0de68ba84dd3887e8ee9922ed48117b28b9dd9`.
The native output DLL is unchanged from the hash in the stage-1 table below.
Both installed hashes match the package and x86 build.

Target is the same verified Tweaks folder below. Runtime backup:
`.wheel-settings-backups/20260919-192056-090-050f7aba/receipt.json`.
All four original configuration hashes below remain unchanged. The profile
added by stage 1 is also retained unchanged, SHA256
`43c45a9d57a8f259db7fcb74922644546eb5d344ffe3fd5d04700be1388f921e`.
No files were seeded during this second install; no user binding/tune was edited.

The x86 build and offline parser/UI/input/fake-ABI suites pass. Real-parser
cases cover missing FFB sections, Off-only overrides, legacy indices and saved
GUIDs. Source fixtures cover independent/shared/missing pedal identities and
primary adoption with pedal-source preservation. There are 30 actual ImGui
draw-data PNGs at
`build/wheel-settings-fixture/run-500e6b16b92f4cf0b130604166e18925`.
Coverage is 166 parsed settings / 194 template keys (28 known-dead upstream
CDTracks entries). Live startup/camera identification is pending the next
serialized zero-output slot; physical wheel/force acceptance remains pending.

## Stage 1: superseded and rolled back

**Rolled back after startup validation.** At 19:08:58 UTC the first authorized
zero-output launch failed with Windows `0xc0000142` before a game window. Its
log ended inside base `Settings::read`. The migration code called
`INIReader::Keys("FFB")` for an older upstream INI with no FFB section; that
library throws for absent sections. The source fix and real-parser regression passed in the successor builds. This package must not be reused.

The application-error dialog was closed normally; process exit was verified.
The installer restored the previous upstream proxy from the receipt below and
removed the added WheelFfb DLL. The temporary explicit FFB-Off test override
was removed, and every pre-test configuration hash was verified unchanged.
No camera framing was observed and no force device initialized. Test preflight
settings/log evidence is in `build/camera-live-stage1/`.

The following records the original installation before that rollback:

Installed at **2026-09-19 18:58:54 UTC**. This is a local development candidate,
not live wheel acceptance or a public release. The game was closed; no game was
launched, no input injected and no force applied by this work.

Source: `06b94f55e62edf110f450ab9862a2cbd91068ed4`, branch
`codex/ux-simple-settings-2026-09-16`. Calibration/UI changes: `2d20e1f`.
The source was clean when the verified package was created.

Target:
`E:/Source/launchbox/Launchbox-Racing/Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Tweaks)`.
LaunchBox's Windows platform record and the actual x86 `OR2006C2C.EXE` identify
this install. Executable SHA256 remains
`68ceb386829066f8455b9d027320af962584321f3e2e8a79c72841495a6134c3`.

Package: `build/packages/wheel-settings-20260919-stage1-verified.zip`.
SHA256: `e448ee211c06e18fd6bb005705ada4b7cceb39453388a14506c17c3ed757c6c2`.
The earlier `wheel-settings-20260919-stage1.zip` was not deployed; CMake's
generated header ordering was committed before producing the verified package.

| Installed file | SHA256 |
|---|---|
| dinput8.dll | `c5a7068638e01f93301a38e06cddd21b22b986aa663f754f1d5488794f0f7cb8` |
| WheelFfb.dll | `95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31` |

Both installed hashes match the package and build output. The installer
recognized existing OutRun2006Tweaks **0.6.1.0**. Previous proxy SHA256:
`116bc609d2af4b366f7f02e3594f7d3be1e57c2f1fd937d9b8f36b9df1cf5cf9`.
No previous `WheelFfb.dll` existed.

Backup/receipt below the target:
`.wheel-settings-backups/20260919-185854-286-07569bf0/receipt.json`.
The directory holds the verified old proxy and settings snapshot. Restore from
the package's Install.ps1 reinstates that proxy and removes the added output
DLL, while retaining current settings. Instructions are in
[INSTALL-WHEEL-SETTINGS.md](INSTALL-WHEEL-SETTINGS.md).

All four pre-existing configuration/manifest files were retained byte for byte:

| File | Retained SHA256 |
|---|---|
| OutRun2006Tweaks.ini | `741ada5c9ccbab64c259d20b65c5959e8292a072b96a0d3b41cff8551a615158` |
| OutRun2006Tweaks.imgui.ini | `c8571b8b8bee7c454eca08558333d3435b08a17aaf45d70479a1ed21c492fddf` |
| OutRun2006Tweaks.lods.ini | `4e943cc0fd9282bd84452b5113656a60e1776fd4120406477668ce1b56cac0e5` |
| redux-manifest.json | `5b112ed42dd7e38d4ad38a32c6278fbd563c03fbf1971da567448af910896204` |

Only the absent `force-profiles.ini` was seeded. There was no user-override or
binding INI to migrate. Existing `UseNewInput=false` remains untouched. Since
this older base INI has no wheel section, wheel remapping remains the safe
default Off until the owner enables it from F6 Setup for the next launch.

Evidence: x86 Release build; `Test-WheelSettings.ps1` (transactional calibration,
144 legacy normalization cases, fake-ABI output gates, 29 actual ImGui frames);
`Test-WheelInstall.ps1` (retention/restore/rollback/tamper); INI coverage
162 reads/190 template keys; `git diff --check`. PNGs/manifest:
`build/wheel-settings-fixture/run-1de7275af5234449ada49abd835d1c2f`.

Independent pedal devices and camera integration are successor work. Physical
controls, focus/game ownership, FFB feel and native high-DPI behavior are still
untested. See [the complete inventory](UX-OVERNIGHT-2026-09-16.md).
