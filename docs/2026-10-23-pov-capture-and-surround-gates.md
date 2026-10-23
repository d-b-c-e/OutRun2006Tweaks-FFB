# F6 POV capture and Surround source inspection, October 23

Garu, Miho339636/340080; isolated `garu/outrun-triples-prompts-20261023` from
canonical `654ccf9`. Reha339787 reports no conflicting OutRun Tweaks claims.
No game, device, display, install or force-tuning change.

## Demonstrated F6 capture defect and correction

The runtime button decoder and profile writer already accept POV directions
`128..143`, and F6's saved-binding rows already label them by hat/direction.
However, `UiSnapshot` contained only 128 physical buttons, capture scanned only
those buttons, and Save rejected every candidate at or above 128. A wheel hat
could drive a configured action but could not be captured by the F6 prompt.

The snapshot now includes all existing binding codes and reads them through the
same `IsButtonPressed` decoder used by gameplay. Capture scans that snapshot,
previews `POV 4 Left` instead of `Button 144`, and requires release before Save.
The persisted encoding is unchanged. Diagonals expose both neighboring
directions and use the existing ambiguity/release latch. Held-at-start inputs,
identity/conflict checks and atomic-save failure handling retain their guards.
H-pattern gear/reverse capture remains physical-button-only, including explicit
Save refusal; sequential Shift up/down may use a POV as the runtime already can.
No gameplay dispatch, H-pattern logic, force model/profile, defaults or external
ABI changes. The expanded snapshot is an internal source type, not a saved format.

Focused x86 checks use the existing `input_calibration_fixture.cpp` and
`wheel_settings_fixture.cpp` targets/arguments from `Test-WheelSettings.ps1`,
with external headers, ImGui sources and existing static libraries from the
canonical checkout. No full app build or dependency download. Test-only inert
RemapInject definitions restore the input fixture's current linker dependencies;
they cannot arm injection, and an unexpected Poll/Deliver aborts the fixture.

- Before the production change, the raw-POV case fails:
  `POV direction must reach the UI capture snapshot`.
- After: all 16 cardinal directions, diagonal exposure, centered release and
  physical button 128 coexistence pass through the actual snapshot producer.
  Existing query, calibration, identity and lifecycle cases also pass.
- The actual F6 ImGui fixture passes POV preview text, held/released Save,
  diagonal rearm, held-at-start latch, save failure/retry, unchanged INI encoding,
  conflict refusal and sequential-versus-H-pattern behavior. Existing UI,
  calibration, preservation and Stop FFB cases pass.

Ignored evidence: `build/pov-capture/{check.ps1,input-before.log,input-after.log,ui-final.log}`;
actual draw data is `build/pov-capture/ui-final/synthetic/1280-pov-capture-simple.json`.
This is mocked-input/software UI evidence, not game rendering or physical-wheel
acceptance. Optional bitmap rasterization was skipped because the available
Python interpreter lacks NumPy; no dependency was installed.

## Surround remains a runtime gate

Current Tweaks source provides planar spanning, not the separate decomp product's
angled renderer. `[Triple] Screens=Surround` uses the normal primary logical
display path; `Separate monitors` can select the three-monitor rectangle.
The stock INI can override default dimensions, so desktop topology alone does
not establish the final client/back-buffer size.
The existing `FixZBufferPrecision` camera hook changes near clip, then calls the
original matrix calculation; it introduces no FOV/projection override.

The installed supported PC EXE still hashes to
`68ceb386829066f8455b9d027320af962584321f3e2e8a79c72841495a6134c3`.
REA confirms PE/x86 identity; its metadata-only session was closed. A bounded
file-byte decode verifies camera RVA `0x84BD0` calls angle helper `0x83C10`
and projection wrapper `0xA980`. The angle helper reads configured width/height
at RVAs `0x340C8C/0x340C90`, a half-angle factor `0.5`, and reference factor
`0.75`; it derives an aspect-dependent horizontal angle at camera `+0xA4` and
vertical angle at `+0xA0`. This does not independently qualify the live matrix,
every camera mode, culling, HUD or D3D output. No concrete projection defect was
established, so no speculative camera patch was made. All 10 pinned byte anchors
pass; `build/pov-capture/{check-projection.py,projection-anchors.json,projection-trace.txt,projection-check.log}`
retains the exact source evidence.

The remaining usable-Surround gate is an authorized in-race run on an actual
logical NVIDIA Surround display: matching full-span client/back-buffer bounds,
wide race image, camera switches, center-screen menus/HUD and side-edge object
culling. Prior title/attract or single-view moving-camera captures and the
separate decomp renderer do not pass that gate. Force tune and H-pattern remain
unchanged, and no claim is made for angle-correct three-view rendering here.
