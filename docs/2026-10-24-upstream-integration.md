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
