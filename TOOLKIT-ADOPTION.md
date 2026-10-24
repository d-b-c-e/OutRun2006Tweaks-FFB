# Toolkit standards adoption

Which entries of the wheel toolkit's standards ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) this mod has brought in.
Update a row in the same commit that adopts it. Statuses: `adopted`, `partial`,
`pending`, `n/a` (say why), `unchecked` (nobody has looked yet).
Seeded 2026-10-04 from what was verified that day; `unchecked` rows need a look.

| Standard | Title | Status | Notes |
|---|---|---|---|
| STD-001 | One mod per game | unchecked |  |
| STD-002 | Recording and playback from launch | n/a | Superseded by STD-012; input determinism is not established. |
| STD-003 | Normalized FFB strength | unchecked |  |
| STD-004 | Consistent settings UX | partial (source) | October 23 focused F6 POV capture/preview/save source fixtures pass; H-pattern POV gears remain refused. Help now has small mod/Wheelkit links and a distinct Submit issue action, but support-file preparation and rendered review remain open. See docs/2026-10-23-pov-capture-and-surround-gates.md. |
| STD-005 | Camera numpad layout 8/2 9/3 4/6 7/1 +/- 5 | unchecked |  |
| STD-006 | Camera step sizes are settings | unchecked |  |
| STD-007 | Triple screens in one wide window | partial (source) | f4e9f01: one borderless window, planar wide view (Surround native; separate monitors via [Triple] Screens). October 23 pinned camera-byte inspection establishes no projection fix; actual Surround race/back-buffer/HUD/culling remains open. Not angle-correct side views. |
| STD-008 | Display changes: game applies once | unchecked |  |
| STD-009 | Dashboard telemetry matches the HUD | partial | Supported-executable HUD base +0x1F8 agrees with three October 8 Dino km/h frames under muted delivery. Source candidate replaces Forza raw*90 with validated HUD-base/3.6; 41 encoded packet checks and full build pass. Installed runtime still has the old estimate; live UDP/all-car/mph qualification pending. See docs/2026-10-08-hud-speed-discovery.md. |
| STD-010 | Install the latest build for testing | adopted | Reviewed private e5b44f8 installed October 8 with 41-export native 50ba139; both runtime hashes and five retained INIs verified. docs/2026-10-08-force-delivery-acknowledgment.md. Later schema-3 source awaits review/live input observation; no physical acceptance inferred. |
| STD-011 | Work lands on main | adopted | This scoped playback intake was merged/pushed to master; accepted runtime remains independently identified. |
| STD-012 | Reproduce the route and preserve original signals | partial | Installed 5b19c9d: process-lifetime output mute and external original-force arm/save qualified with 3,601 live rows and exact request/state replay; Claude independently repeated replay and owner-file hashes. Software constant-fallback route only, no native output. Companion discovery has 3,600 moving pairs. Complete pose ownership, speed units and gameplay replay remain open; intermittent menu freeze unresolved. See docs/2026-10-08-muted-force-capture.md. |
| STD-013 | The installed build launches plainly | partial | Repaired copy and prior f021736 passed ordinary startup/offline entry. Claude's 069fe41 plain-launch discovery reached Palm Beach again; retained frames and eight restored owner files independently checked, exit 0 remains reported. Diagnostic pad mappings were temporary; normal owner-wheel driving remains unqualified. |
| STD-014 | Request reciprocal review when progress stalls | adopted | Reviewed Claude's focused retries, reproduced mapping and update-sampling defects, and prepared a tested input candidate for reciprocal review. The responsive probe is not a frozen-process stack; no speculative lifecycle-lock change. See docs/2026-10-06-focused-input-followup.md. |
| STD-015 | Triples on Surround and separate monitors | partial (source) | SPN planar only: [Triple] Screens = Separate monitors spans the borderless window over three equal side-by-side monitors (f4e9f01, topology fixture Test-TripleSpan.ps1). Not projected triples; native in-race aspect at 7680 and independent projected views remain unqualified. |
| STD-016 | Forza Horizon telemetry on by default | partial (source) | Candidate e291000 defaults telemetry on and retains explicit `Enable = false`; the source-linked INI fixture covers missing and explicit values. Encoder/HUD agreement, the product build and installed runtime remain open. |
| STD-017 | Hide empty settings pages | unchecked | Current panel has not been audited against this newer standard. |
| STD-018 | Handling changes never reach online scores | unchecked | Online/offline eligibility and score paths must be established before any gameplay replay or handling change. |
| STD-019 | Centre menus and gameplay HUD | unchecked | Title/attract images do not qualify menus, loading, F6 and in-race HUD across the requested layouts. |
| STD-020 | Standard portfolio feature checklist | partial | All 25 ledger rows explicit as of 2026-10-07; missing STD-021..025 added during discovery review. Pending/unchecked features require evidence, not automatic adoption. |
| STD-021 | art of rally is the FFB reference | pending | No matched original OutRun force capture or attended comparison against Art at 50; no gain change from discovery. |
| STD-022 | One triple-screen selector | partial (config) | OutRun2006Tweaks.ini [Triple] Screens = Off / Surround / Separate monitors (planar span; Off and Surround leave the window as before). No F6 control yet; projected views not implemented. |
| STD-023 | Frame-rate readout in settings and log | unchecked | Existing framerate hooks have not been audited for the standard 10 s mean/1% low/worst summary and 30 s log. |
| STD-024 | Optional on-screen frame-rate counter | unchecked | Saved default-off counter, centre-screen placement and render cost have not been audited. |
| STD-025 | One shared force model for tyre games | pending | Current legacy custom model remains. October 23 fixes the optional shared adapter's crash cooldown with source fixtures only; no tune/default or runtime adoption change. Research available physical inputs before an AxleForceCurve adapter or a documented arcade exception; named pose fields do not establish tyre-force units. |
| STD-026 | Player-facing tuning is deliberate | unchecked | Existing Simple/Advanced tuning needs review against the current standard before adding controls. |
| STD-027 | Independent steering and crash strength | pending | Legacy model composition still needs an explicit split and versioned replay contract; no label-only adoption. |
| STD-028 | Complete owner registry snapshots | n/a | Current native-game checks preserve INI/save files and do not snapshot or delete a Unity registry leaf. Any future registry use must use the shared raw helper. |

October 8 discovery source adds camera observations to the existing bounded
window (schema 2, 334 fixture checks and Release build pass). It does not promote
STD-012/015: no pose writer, producer-preserving mute or projected rendering is
implemented by this change. See `docs/TICK-DISCOVERY.md`.

The later schema-3 source records raw force inputs and requested settings even
with FFB Off, using the unchanged surface aggregation/configuration shared with
the existing calculation. 402 discovery checks, 12 analyzer tests and original
legacy calculation replay pass. Original command/state capture under mute and
gameplay replay are still unfinished; STD-012/021/025/027 stay pending.

The subsequent software-force candidate adds a process-lifetime output mute and
bounded original legacy producer capture, with explicit virtual admission and
V3 exact replay. Existing V2 replay remains unchanged. It passes synthetic
production/lifecycle fixtures but awaits peer review and a live moving capture;
STD-012 remains pending for gameplay replay and live qualification. No force
normalization, independent-strength composition or new model is claimed. See
`docs/2026-10-08-muted-force-capture.md`.

October 8 force delivery follow-up preserves STD-012's distinction between
requested signals and accepted output: refused native updates no longer advance
the accepted-force cache and release/latch output until an explicit retry.
Production fake ABI and legacy signal replay pass. The reviewed `e5b44f8` private
package is installed with five INI files retained; transient access/effect loss
recovers through accepted neutral before a new warmup, permanent refusal latches.
Physical recovery and original muted capture remain untested; STD-012 remains pending. See
`docs/2026-10-08-force-delivery-acknowledgment.md`.

The historical pre-capture statements above are superseded by the October 8
14:23 CT original capture (`5b19c9d`). Producer-preserving mute and software
force recording/recalculation now pass; physical delivery, independent strengths
and gameplay playback remain separate gaps. Source `3fac6d0` adds passive HUD
speed discovery with 410 checks/14 analyzer tests/Release build, not yet installed.

STD-033 (rig-profile controls), 2026-10-10, Claude: source only, not packaged or installed. `src/profile_controls.cpp`
reads the profile from `[WheelkitProfile]` in `OutRun2006Tweaks.user.ini` (Tweaks already owns `[Controls]`; no new file,
per Astra 3507). The remap's button keys also take POV directions (`src/pov_binding.hpp`). A new revision becomes the DirectInput remap's `.user.ini` keys in one atomic edit before the INIs are read.
`tools/Test-ProfileControls.ps1`: 75 offline checks, MSVC x86 `/W4 /WX`. In-game observation and owner acceptance
pending.
Capability receipt (2026-10-10): `tools/controls-capability.json` lists the exact runtime commits whose adapter passed
review (none yet). Package-WheelSettings declares `controlsProfileSchema`/`controlsAdapter` in the manifest only for
those commits. Install-WheelSettings writes `dbce-outrun2006-controls.json` in the game folder with the installed
dinput8/WheelFfb hashes. A package without the capability withdraws the receipt, a failed install rolls it back, and
Restore returns it with the runtime. `tools/tests/Test-ControlsCapability.ps1`: 24 checks, PowerShell 7 and 5.1.
