# Changelog

## Unreleased — wheel settings candidate, 2026-09-19

- Added Simple shifter/button-box device capture, Sequential/H-pattern mode,
  Settings/Stop rebinding, same-context conflicts and retryable Clear.
- Added automatic viewport scaling for readable default 4K settings.
- Isolated actual stock/SDL/DirectInput dispatch while settings/capture are open
  or focus is lost, with held menu input release before recovery.
- Added F6 Simple-first wheel settings, remembered Advanced view, persistent
  Stop FFB/F8, ordinary primary-wheel/button binding and calibration.
- Calibration captures center/rest and full travel, previews inversion/deadzone,
  then saves identity, axis and endpoints together. Existing bindings and
  uncalibrated transforms remain unchanged until an explicit successful save.
- Added an FFB device dropdown following saved steering identity by default,
  exact explicit overrides, missing-device refusal and zero-before-switch.
- Added offline production ImGui raster fixtures at 720p/4K, input calibration
  checks and fake-ABI output-gate tests.
- Added independent Throttle/Brake device dropdowns inside Bind, exact saved
  identities, shared pedal-handle reuse and neutral disconnected input.
- Steering can also select a replacement device in Bind; the previous pedal
  identities/calibrations survive the swap, and force output is zeroed first.
- Calibrated pedals on the primary wheel now fail neutral on disconnect in
  both current and previous-frame input; telemetry reports unavailable.
- Fixed startup with an older upstream INI lacking an FFB section; regression
  exercises the actual INI parser and layered migration cases.
- Added a local install/restore package that backs up recognized Tweaks runtime
  and preserves existing settings, bindings and profiles.
- Pinned only the native output DLL/header to official toolkit v0.13.0/native
  0.6.0; retained v0.8.0 force model, profiles and encoder unchanged.

Custom camera mounts remain follow-up work.
Physical wheel/force behavior has not been accepted in game.
