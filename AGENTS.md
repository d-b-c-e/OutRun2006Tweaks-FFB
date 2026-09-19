# OutRun2006Tweaks-FFB working notes

Read [CLAUDE.md](CLAUDE.md) for build architecture and known force-signal issues,
then [the UX adoption inventory](docs/UX-OVERNIGHT-2026-09-16.md) and the latest
deployment receipt under `docs/`. Offline fixtures and deployment are not proof
of live controls, physical force or camera acceptance.

- `src/overlay/wheel_settings.cpp`: the production F6 Simple/Advanced UI and
  atomic user-INI saves. One shared Settings store; no backend change on view
  switches. Stop FFB/F8 persists Off.
- `src/axis_calibration.hpp`: finite/range validation and opt-in normalized
  axes. Existing uncalibrated transforms must remain unchanged. Binding, identity
  and calibration save together; failed/cancelled captures never flush later.
- `src/hooks_inputremap.cpp`: game input adaptation. Do not switch the SDL and
  DirectInput backends at runtime. Device and camera capability gaps are listed
  honestly in the inventory; do not invent a game handbrake route.
- `src/wheel_input_gate.cpp/.hpp`: final input dispatch isolation, installed
  after all adapter hooks. Keep hardware polling during settings; release-latch
  both digital and analog menu navigation. The x86 fixture uses real local
  trampolines and an explicit `asInvoker` manifest, never game/device injection.
  `SuppressTextMessage` is called after ImGui's actual Win32 handler; stock
  WM_CHAR entry bypasses switch dispatch. Read-only Advanced Help counters and
  the executed baseline comparison are in `docs/NATIVE-INPUT-DIAGNOSIS-2026-09-19.md`.
- `src/hooks_dinputffb.cpp`: game force signals and output gates. Preserve owner
  tunes. Native output is a reviewed **v0.13.0 override** on the **v0.8.0 model/
  profile/encoder baseline**. Read `lib/toolkit/NATIVE-PROVENANCE.json` before sync.
- `tools/tests/Test-WheelSettings.ps1`: real ImGui/memory-only input/fake ABI
  tests. `render_imgui_fixture.py <run-folder>` renders actual ImGui draw data
  with NumPy/Pillow. It does not launch a game or acquire a wheel.
- `tools/tests/Test-WheelInstall.ps1`: synthetic-executable installer/restore,
  retention and rollback tests. `tools/Check-IniCoverage.ps1` checks every key.
- `tools/tests/Test-WheelInstallPlayer.ps1`: actual Windows PowerShell 5.1
  `-File` and shipped batch routes from another CWD, with spaced paths, omitted
  package path, prompted game path, restore, explicit override and failures.
  Use `-PackagedDirectory` to test an immutable package with a synthetic EXE.
  The child uses its own default PS5.1 module path, not inherited PS7 modules.
- `tools/Package-WheelSettings.ps1` packages the existing x86 Release build;
  package `Install.bat` calls `Install.ps1`. Deploy only while the game is closed,
  verify hashes and preserve owner settings. Never kill a game for deployment.
  For installer-only repacks, use `-RuntimePackageDirectory` pointing at the
  verified frozen package. `sourceCommit`/`runtimeSourceCommit` retain its runtime
  identity; `installerSourceCommit`/`packagingSourceCommit` identify the repack.

Build: `cmake --build build --config Release --target outrun2006tweaks`.
On this machine CMake is under VS2022 BuildTools `Common7/IDE/CommonExtensions/
Microsoft/CMake/CMake/bin`. Commit/push changes with `[skip ci]` when hosted CI
is not requested. Live game/device tests require the coordinator's serial slot;
no unattended nonzero force.

For a live smoke, launch the EXE from its game folder (or set shortcut Start in).
A direct launch without that working directory showed blank game frames despite
a working F6 panel. Stage 5 verified actual 4K F6/pages and shortcut capture/
cancel. Stage 6 reproduced native title confirmation failure on both the new
candidate and exact saved pre-session proxy, with two Return window messages
and zero release-blocked queries. Exact Confirm edge counters were zero, so
do not infer successful native keyboard sampling or a specific root cause.
Actual 4K Advanced Help was readable. All owner state was restored after both
normal exits; current installed source is d3d7f23. Player cameras, native text
entry isolation and physical wheel/force remain unaccepted. See the deployment
receipt and diagnosis result before repeating a live test.
