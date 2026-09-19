# Local deployment — 2026-09-19, stage 1

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
