# Status: abandoned in favor of the decompilation port (2026-11-01)

The owner has decided that OutRun 2006 work continues in the native decompilation port,
`outrun2-decomp` (https://github.com/d-b-c-e/outrun2-decomp, local `E:\Source\experiments\outrun2-decomp`),
not in this Tweaks-based mod. No further features or releases are planned here.

- Installed as-is on the owner machine: d4580da (dinput8 EB6F83E8, Wheelkit receipt kept). Rollback C9600A58.
- Unshipped work kept for reference: the three-view spike (`DBCE_OUTRUN_TRIPLE_VIEWS`, camera fn `0x484BD0` fed per panel),
  F7 Original/Angle with a saved `[Triple] ViewMode`, rig-geometry turn, gimbal-lock fix f68bead, clean-install Phase A tooling.
- Findings that may help the port: the main camera is computed once per frame from RVA `0x85B2C` in the tick; fov at +A0, aspect +B8, shift +C4;
  the game saves/restores the viewport, so a viewport remap must be undone in GetViewport.
