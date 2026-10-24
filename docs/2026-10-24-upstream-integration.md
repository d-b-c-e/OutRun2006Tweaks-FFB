# OutRun Tweaks integration candidate — October 24

Branch `miho/outrun-upstream-20261024` merges DBCE source `986353a` with
upstream Tweaks `270cf935` at `9e4ce4bc5c468e2e543d7d5a83a9e8e33d8b8883`.
The branch is pushed; it is not on main and is not installed into a game.

The source includes upstream's current input system, frame interpolation,
overlay/settings UI, graphics and other Tweaks changes while retaining the DBCE
wheel remap, F6 settings, FFB, telemetry, triple-screen span and support paths.
The legacy wheel/FFB INI reader runs after the new registered-settings reader.
The Tweaks overlay now edits its registered keys without replacing unrelated
user INI bytes; the offline test covers wheel calibration, unknown keys,
comments, revert-to-default and a refused write. SDL input remains Off by
default so existing wheel profiles keep their route unless explicitly changed.

Configured x86 Release build passed. Candidate `build/upstream-merge/bin/dinput8.dll`
SHA-256: `E788EAD1D5F27CBFEDE2D91B9DDB40E5CF5B34998838033F2A62F09A78CECAD5`.
The bundled `WheelFfb.dll` SHA-256 is
`432727681B1A8A6F62D68F2E1ED865FF9F229AECCC628B82FA97147626451968`.
The checked-in INI coverage check passes (177 read settings, 203 shipped keys),
as do the triple-span fixture, 107 profile-control checks and the complete
wheel-settings fixture (F6, input, force and calibration cases). All were
device-free; the build/test leases were released.

Still open: private package/installer checks, ordinary game startup, driving
input, overlay coexistence, frame interpolation, graphics and FFB runtime
checks, physical wheel behavior and owner acceptance. These are required
before main/deployment or a parity claim. The upstream merge also brings
its current license; assess the combined distribution terms before a public
release.

## Private package check

The current branch was rebuilt as x86 Release and packaged through the existing
review-only route. The ZIP is
`build/packages/miho-upstream-review-20261024-100011.zip`, SHA-256
`4CD55AFA8BA3AB5D0F88E3333677E0251BEB6CD0B2D938E80665917E4CBD8C72`.
Its `dinput8.dll` is
`72778692F3BBF0140AAA66475784DD668653E8BFD128BA96385FBB61D724AF2D`.
The package manifest records build source commit `17ca4d6` and
`reviewOnly=true`.

The package inventory passed 58 checks, including notice hashes and tamper
refusals. The actual packaged installer passed its PowerShell 5.1 and batch
fixtures, including settings retention, restore and rollback. The checks used
a synthetic game executable; the ZIP was not installed or launched in OutRun.
The shared build/test lease was released. Next is a private-copy startup and
driving check, with overlay, frame interpolation, graphics and wheel/force
behavior observed separately. The unresolved distribution gates still prevent
a normal release package.

## Private startup and router-rule ownership

The review package was installed only in a new private copy at
`E:/outrun2-research/private-upstream-9e4ce4b-20261024/game`.
All 13,103 copied files matched the archived game by SHA-256; the executable is
`68CEB386829066F8455B9D027320AF962584321F3E2E8A79C72841495A6134C3`.
The package installer retained the five existing settings files byte-for-byte,
backed up the prior Tweaks runtime, and verified its new proxy
`72778692F3BBF0140AAA66475784DD668653E8BFD128BA96385FBB61D724AF2D`.

A 35-second private title run opened the OutRun window, logged a fresh
`DBCE_OUTRUN_SIGNAL_MUTE=legacy` process mute, and closed cleanly. The lease was
released. Evidence is in `startup-01` under that research directory. This
qualifies startup only: no driving, F6, F11, graphics, interpolation, device or
physical force check was made.

The run exposed a Tweaks UPnP side effect: it requested six router port rules
despite the process mute, and readback afterward found all six pointing to this
PC. An older OutRun log records successful mapping on October 5, but there is
no pre-run router snapshot proving which rules existed immediately before this
check. They were left in place to avoid deleting possible owner rules.

The following source revision blocks UPnP requests in muted processes. In
normal play it reads each rule before creating one, never replaces another
endpoint's rule, and on ordinary exit removes only rules this process created
and still owns. The x86 Release build produced proxy
`BFE36C55DDADCC8A166D2458C20565A8964DBEB38DA2FC59BCAD761673C6ABB7`.
The synthetic host-lifecycle fixture passed its once-only cleanup assertion;
the focused UPnP fixture passed preexisting, conflicting, unreadable, new-rule,
reassigned-rule, and muted-process cases. These checks do not prove router
behavior of the revised build in a live game. A new private package and runtime
check remain open, followed by driving, overlay coexistence, interpolation,
graphics, wheel/force and release review.
