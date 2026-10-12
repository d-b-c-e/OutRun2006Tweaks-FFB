# Shared-model path: crash and gear-shift timers never count down (2026-10-12)

Found while porting the production force calculation to the OutRun 2006 decomp (`research/outrun2-decomp`
`windows/source/ffb_outrun`, "outrun-legacy@1"). A note only: no runtime or tune change here.

**Where.** `src/ffb_calculation.inl` at `34b2040`, `CalculateSignals`:
- `crashImpulseTimer` is set to 90 by the crash detectors (the speed-window check and the `field_8 & 0x1000` edge)
  for both branches, but it is decremented only inside the legacy branch (`crashImpulseTimer--;`, line 313).
- `gearShiftTimer` is likewise only set and decremented in the legacy branch (line 326).
- The shared-model branch (`if (ffbLoaded && useSharedModel)`, from line 210) reads `crashImpulseTimer == 90` to
  pass `in.impact`.

**Effect, with `FFBProfile = arcade-outrun@1` only.** After the first crash the timer stays at 90, so:
- `in.impact` is handed to the shared model on every tick (`crashImpulseTimer == 90` stays true);
- both detectors require `crashImpulseTimer <= 0`, so no later crash is detected.

The default `FFBProfile = legacy` (what the owner runs) is not affected: its branch counts the timer down.

**Not verified at the rig.** The behaviour follows from the source; the fixture (`signal_calculation_fixture.cpp`) runs
only the legacy branch (`useSharedModel = false`), so nothing here exercises it.

**Possible fix, for whoever owns the shared path** (not applied): count the timers down once per tick outside the
model branches (or in the shared branch too), and add a fixture case with `useSharedModel = true` and two crashes.

Also seen while building the fixture at `34b2040`: it no longer links (`RemapInject::Observe` unresolved,
referenced from `FFB::ProcessMutedSignals`), so `tools/tests/Test-SignalCalculation.ps1` fails at head.
