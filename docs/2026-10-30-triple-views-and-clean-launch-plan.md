# Three projected views (perspective, curve) and the clean first launch: plan, October 30

Plan only: nothing here is built or run. Source state 851427f (installed runtime d4580da).

## 1. Three projected views (STD-007): spike plan

### What exists

- The mod draws one planar span: `[Triple] Screens = Surround` or `Separate monitors` is one wide frustum. There are no
  draw-level Direct3D 9 hooks in this fork (only device/present parameters).
- The camera seam that is known here, `CalcCameraMatrix` at RVA `0x84BD0` (called by `FixZBufferPrecision` on the
  camera object at `0x39FE10`), also runs for scene effects (`0xBE70`). Changing it gives one turned or wider frustum,
  not three views.
- The OutRun 2 decomp project reads the same `OR2006C2C.EXE` (it hashes every code range it uses), so its function
  map applies to this executable. It shows:
  - `0x484BD0` (= RVA `0x84BD0`) runs the projection `0x404310` and then the view `0x482F20`;
  - the projection is already off-axis: `D3DXMatrixPerspectiveOffCenterRH` from the camera's fov `+A0`, aspect
    `+B8`, near/far `+BC`/`+C0` and shift `+C4`/`+C8`; it writes the frustum globals `0x95BF40` and the four culling
    planes `0x95BF58` from the same bounds, so culling follows the view by construction;
  - a side view is the same camera with a yaw before `0x482F20`'s rotations and a horizontal shift.
- The decomp's own three-view spike replicates draws in its Direct3D 11 backend (`triple_bind`), which it owns. It is
  default-off and has never run in the game.

### Recommended route: replicate the 3D pass, not the draws

Draw replication needs, for every vertex shader program, the constant registers that carry the clip transform, plus
exceptions for 2D and post effects. The game's camera already supports an off-axis projection and recomputes its
culling planes, so the smaller change is to run the 3D scene pass three times:

1. For each panel (left, centre, right): viewport = that third of the back buffer; camera `+B8` = the panel's aspect,
   `+C4` = its shift, plus its yaw (toolkit rig: panel widths, bezels, side angle, eye distance); call `0x484BD0`;
   draw the 3D scene.
2. Restore the camera fields after the last pass. The 2D/HUD pass runs once, on the centre third.
3. State that is single-view today is computed once from the centre unless a pass shows it is wrong: car reflection
   cube, stencil shadows, lens flare, particle screen mapping (`0x418CF0`), `ScreenEdgeCullFix` bounds.

### Steps (each a small commit; default off; development switch only)

1. **Offline address check.** A test that reads the installed executable's bytes at `0x404310`, `0x482F20`,
   `0x484BD0` and the scene-pass function, and compares them with the decomp's hashed ranges. No game run.
2. **Observation build (rig, muted, focused once).** Count calls to `0x484BD0` per frame with their return
   addresses, and log the 3D pass entry/exit, to find the main-view call and the pass boundary (scene effects call
   `0x484BD0` extra). Log only; nothing changed.
3. **Spike.** Behind an environment switch, `Separate monitors` only: three passes as above. The log reports
   `three-projections` only from three observed passes per frame (a counter).
4. **Rig check (muted).** Pass counter, frame rate at 48:9 against the planar span (A/B), window captures by handle of
   the three thirds. Angles and seams need the owner at the rig.
5. **Then curve.** With three views, the toolkit's curved presenter contract per panel (`CurveRadiusMm`), and F7
   Original / Angle / Angle + curve saved as the framing policy rev 3 describes.

Cost: about three times the 3D draw calls; fill rate is the same as the planar span. Risk: the pass boundary is not
yet located in this executable (step 2 finds it).

## 2. Clean-install Phase B (first launch of a clean copy): plan

Phase A (file level) passes: `tools/tests/Test-PackageCleanStart.ps1`, 16/16. Phase B is **not run** until the owner
decides the isolation and save protection.

The game does not use Steam (no Steam API in the executable or folder). Its state lives in the game folder: `SaveGame\`,
`Ghosts\`, `OnlineLoginData.dat`, the `OutRun2006Tweaks*.ini` settings and the log; the mod's test flags live in
`%LOCALAPPDATA%\dbce\outrun2006`. No OutRun registry keys were found.

What Phase B needs:

1. **A clean copy:** the game files without the mod (no `dinput8.dll`, `WheelFfb.dll`, `OutRun2006Tweaks*.ini`,
   receipt or backups) and without the player's saves, about 4 GB, in a disposable scratch folder (never inside the games library). Open
   question for the owner: which folder is the pristine source (the two install records still disagree).
2. **Isolation:** the copy has its own saves; `%LOCALAPPDATA%\dbce\outrun2006` must hold no `inject.on` or
   `record.on`; the owner's folder is hash-snapshotted before and compared after (it must not change).
3. **The run:** install the current package with its installer into the copy, launch once (signal-muted, focused
   once, no OS keys), wait for the remap's `opened` line and the first frame-rate line, close normally.
4. **Checks:** the log shows the mod loaded with defaults, the receipt matches, the copy's new files are only the
   game's own first-launch files plus the seeded settings, and the owner's folder is byte-identical. Then the copy is
   deleted.
