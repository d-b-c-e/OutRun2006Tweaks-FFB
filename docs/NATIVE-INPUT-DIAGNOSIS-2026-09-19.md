# Native title input comparison

Stage 5 starts correctly from the game folder, but two Return taps before first
F6 did not advance the title. A, Space and F2 did not establish a player menu.
The cause is unresolved. A live 4K settings/capture smoke is not proof of native
game input or player-camera framing.

## Offline findings

- The final guard is installed after every adapter. With no blocking reason or
  held requested bit, it returns the original aggregate query result unchanged.
  Native SwitchNow/On returns raw mask bits; SDL has all-bits/chord semantics.
  Production x86 trampolines test both. Axes do not latch digital Confirm.
- Focus/UI suspension latches digital controls; a requested bit clears only when
  the original Now route reports released. Another bit cannot keep Confirm
  latched unless the game asks for both in one aggregate query. No live state
  counters existed in stage 5, so a startup-focus/native-state issue is not ruled
  out by the before-first-F6 check.
- Exact installed EXE SHA256
  `68ceb386829066f8455b9d027320af962584321f3e2e8a79c72841495a6134c3`:
  native WndProc RVA `0x17FCB–0x18045` dispatches WM_CHAR to active text entry
  independently of the switch API. The bundled ImGui Win32 backend also reads
  WM_CHAR, then returns zero. The previous wrapper therefore forwarded it.
- Stage 6 consumes only native WM_CHAR while UI/focus suspension applies, and
  retains captured scan codes through held repeats/queued old characters until
  a fresh unblocked key press. ImGui still receives messages first. Lifecycle,
  Alt+F4 and native polling remain intact. Source-linked fixtures cover the
  message helper; actual WndProc/licence-entry acceptance remains pending.
- Stage 6 Advanced Help shows read-only dispatch totals, suspension reasons,
  held mask, exact Confirm edge queries/positive results, release blocking and
  Return window-message delivery. No extra device poll or native query is made
  for diagnostics. Counters include the preceding closed-panel attempt; merely
  opening diagnostics necessarily sets a UI blocking reason.

## Minimal next serialized test

Use two short launches, both from the verified game folder. Do not change input
backends during a run and do not add a camera hook to work around this failure.

1. Snapshot all owner settings, SaveGame and both installed DLLs. Verify FFB Off,
   telemetry/remap Off, `UseNewInput=false`, `VibrationMode=0`,
   `VibrationStrength=0` and `ImpulseVibrationMode=0` in the temporary override.
   Temporarily keep WheelFfb.dll outside the runtime search location to make
   toolkit force initialization unavailable for this input-only comparison.
2. Launch the reviewed stage-6 candidate. At the settled title, try Return twice
   before F6. Open Advanced Help and capture the diagnostic counters. Normal exit.
3. With the game closed, swap only the proxy to the exact pre-session Tweaks
   baseline from `.wheel-settings-backups/20260919-185854-286-07569bf0/dinput8.dll`.
   Verified SHA256
   `116bc609d2af4b366f7f02e3594f7d3be1e57c2f1fd937d9b8f36b9df1cf5cf9`,
   product Outrun2006Tweaks, file version 0.6.1.0. This is the owner's saved
   pre-session binary, not an independently attested official release. It has no
   WheelFfb/DirectInputFFB/remap strings. Retain the same input configuration,
   resolution, game folder, executable and supported automation method.
4. Repeat the settled-title Return attempts and record whether a menu appears.
   Normal exit, then restore the installed candidate/native DLLs and every
   original setting/save. Archive any test-only files, restore their original
   absence, and verify all hashes before releasing the lease.

If both binaries fail, this narrows the issue to native setup/input delivery or
a shared upstream condition; it does not identify which. If only the candidate
fails, use its reason/raw-positive/release counters to localize the regression.
Return-message counts alone do not prove DirectInput sampled a key. If neither
has established native input, player cameras remain unidentified; attract shots
must not be labeled Bonnet/Bumper. No force, tune or physics comparison is part
of this test.

## Executed comparison — 2026-09-19

The coordinator granted 21:10–21:18 UTC. Both runs completed and the lease was
released early at **21:13:42 UTC**. Startup logs from both binaries show
`UseNewInput=false` and all three vibration settings at zero. WheelFfb.dll was
absent beside the executable for both runs, with its verified copy retained in
the evidence directory outside the game.

Both the stage-6 candidate and exact saved pre-session proxy failed two Return
taps at the settled title. Neither reached a player menu; the ordinary attract
movie followed. The candidate received **two Return window messages**. Its live
Advanced Help frame recorded **31,464 queries, 15,896 forwarded, 15,568 UI-blocked
and zero release-blocked** queries. The current blocking reason was 6, as expected
with settings/overlay open. Exact single-action Confirm edge counters were all
zero: this diagnostic does not identify what other aggregate masks the title
requested, and Return window delivery is not a native DirectInput sample.

The observed release gate did not hold any query. Reproduction on the saved
baseline also rules out a failure unique to the new UX code in this comparison.
It does **not** identify whether native setup, the supported automation's key
delivery, or another shared upstream condition caused the failure. An attended
ordinary keyboard/controller input check is the next useful discriminator; do
not add a speculative camera hook or call this a proven game-camera constraint.
The stock player camera views and actual licence text-entry isolation remain
unverified. No test licence was created and no driving or torque occurred.

Evidence under `build/camera-live-stage6/`:

- `candidate-help-diagnostics-3840.png`: actual 3840x2160 Advanced Help.
- `baseline-after-return-3840.png`: baseline still in title/attract flow.
- `before-baseline/OutRun2006Tweaks.log`, `after/OutRun2006Tweaks.log`: candidate
  and saved-baseline output gates respectively.
- `prepared.json`, `preflight.json`, `baseline-ready.json` and
  `restore-verification.json`: runtime/configuration identities and exact restore.

Both runs exited normally through Alt+F4. Stage-6 proxy/native DLL hashes, all
five original setting files, absent user/layout overrides and the empty SaveGame
were restored and checked before release.
