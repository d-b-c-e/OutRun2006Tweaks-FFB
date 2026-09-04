# Working on this fork

Orientation for whoever picks this up cold. The README says what the mod does;
this says how it is put together, what has actually been measured, and which
traps cost real time.

## What this fork adds

Upstream ([emoose/OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks))
is a `dinput8.dll` wrapper that patches fixes into OutRun 2006. This fork adds
force feedback, Forza telemetry and a DirectInput remap layer, in
`src/hooks_dinputffb.cpp` and `src/hooks_inputremap.cpp`.

It is 32-bit. The game is x86, so everything here is `Win32`, including the
vendored `WheelFfb.dll`.

## The toolkit boundary

`lib/toolkit/` is vendored from
[dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit),
pinned in `lib/toolkit/VERSION`. It carries:

| Path | What it is |
|---|---|
| `include/wheelffb.h` | the C ABI of `WheelFfb.dll`, plus its runtime loader |
| `include/forza_packet.h` | the Forza packet encoder |
| `native/x86/WheelFfb.dll` | the DirectInput device and effect layer |

**Fix device, effect or packet bugs in the toolkit, not here**, then
`tools\Sync-Toolkit.ps1 -Version vX.Y.Z -Parts include,native-x86`, rebuild,
and commit the bumped `lib/toolkit`. Most of what is in that DLL came *from*
this file; a second copy is how this project and art-of-sim-rally ended up
fixing the same bug on different days.

What deliberately stays here is the **force model** — spring, damper, lateral
load, grip factor, weight transfer, crash impulse, gear thunk, the surface LUT,
`tanh`, the slew limiter. That is about OutRun, not about DirectInput.

`WheelFfb.dll` is **loaded at runtime, never imported**: a missing file has to
cost force feedback, not stop the game from starting. CMake copies it beside
`dinput8.dll` after every build.

## Build, deploy, test

```bash
cmake --build build --config Release --target outrun2006tweaks
```

Needs Visual Studio 2022 Build Tools (C++ workload) and CMake; `generate_vs2022.bat`
configures from scratch. Output is `build/bin/` — `dinput8.dll` **and**
`WheelFfb.dll`. Copy both next to `OR2006C2C.EXE`.

Close the game before deploying; it locks the DLL.

**Never hard-kill the game while a wheel is attached.** A running DirectInput
constant-force effect is not released when the process dies, so the base keeps
applying the last torque and the next launch reads as "no force feedback". Exit
through the game, or Alt+F4 — both are covered by the exit guards.

Logs land next to the game exe: `OutRun2006Tweaks.log` (the mod) and
`OutRun2006Tweaks.ffb.log` (the DirectInput layer; `DBCE_FFB_LOG=0` silences it).

## Measured facts — do not re-derive

- **`sub_1149C0` is the game's own surface-roughness LUT**, decompiled from the
  Xbox vibration code: asphalt 0.0 (silent, correctly — smooth tarmac has no
  30 Hz buzz), sand 0.25, grass 0.70, rough 0.85–0.9, water 0.73–0.79. Road
  texture is driven from it, not from a guess.
- **Crash direction comes from lateral history ~8 frames before impact.** The
  collision response corrupts the lateral signal *at* the hit. Steering angle
  was tried for this and abandoned for the same reason.
- **A wall hit is a ~3% speed loss over 6 frames**, not a big per-frame delta;
  the deceleration is spread out.
- **The gear pulse must net to zero.** A one-sided kick reads as "the game
  yanked the wheel sideways"; a real shift jolt is longitudinal.
- **Force feedback and input must not share a device handle.** The remap layer
  polls non-exclusively; force feedback opens its own `EXCLUSIVE | BACKGROUND`
  handle on the same instance GUID. Sharing meant a poll-side `Acquire()`
  destroyed the downloaded effects mid-corner.
- **SDL3 haptics do not work here.** `SDL_UpdateHapticEffect` is silently
  ignored on MOZA/direct-drive bases, and destroy-and-recreate at 60 Hz just
  vibrates. Removed in `eddaa28`; do not reintroduce it.
- **`SetThreadAffinityMask` to core 0 white-screens on Windows 11 24H2** with
  NVIDIA D3D9 threads. Auto-disabled by an OS build check.

## Open, and it needs one lap

**Which `EVWORK_CAR` field is steering.** `field_1D0` is read as position and
`field_1D4` as its derivative, but the 2026-08-11 validation log has |1D0|
below 0.011 across 170 samples while 1D4 reaches 0.54. A derivative cannot
outrun its own integral by fifty times. The spring and damper terms have
therefore been running on roughly 1% of the signal they were tuned for.

Set `FFBDiagnosticLog = true`, drive one lap, read the `FFB STEERSCAN` line: it
prints the observed min/max of `1C8`, `1CC`, `1D0`, `1D4`, `1DC`, `1E0`. The
steering field is the one that reaches about ±1, changes sign with the corner
and returns to zero on the straights. Fix that before tuning anything.

The method error is worth remembering: the old diagnostic sampled steering once
every two seconds while tracking min/max for the rate, and that asymmetry is
what let a signal a hundred times too small pass for a merely quiet one. Track
min/max for anything you intend to reason about.

**Also outstanding:** the migrated force-feedback layer builds and passes the
toolkit's smoke test at a MOZA R12, but has not been driven in game since the
migration.

## Other debt, recorded not fixed

Speed over-reads roughly 2x. No tests.

**`[CDTracks]` does nothing.** `Settings::CDTracks` is declared in `plugin.hpp`
and read in `hooks_audio.cpp`, but nothing in this fork ever populates it, so
its size is always zero and the custom-BGM feature is inert. The 28 entries the
template ships are therefore 28 settings that do nothing. Left in place because
it is upstream's section rather than ours to delete;
`tools\Check-IniCoverage.ps1` exempts it by name with that reason.

## Keep the config and the code in step

```powershell
.	ools\Check-IniCoverage.ps1
```

It compares every `ini.Get("<section>", "<key>")` in `dllmain.cpp` against the
shipped `OutRun2006Tweaks.ini`, both ways. Four whole sections were once
missing from the template - `[DirectInput]`, `[DirectInput.Shifter]`,
`[DirectInput.Aux]` and `[Telemetry]` - which hid the entire wheel remap layer
and the telemetry output from anyone installing the mod. Point it at a deployed
copy to audit an install:

```powershell
.	ools\Check-IniCoverage.ps1 -Ini "<game folder>\OutRun2006Tweaks.ini"
```
