# Changelog

## Unreleased — wheel settings candidate, 2026-09-19

- Added F6 Simple-first wheel settings, remembered Advanced view, persistent
  Stop FFB/F8, ordinary primary-wheel/button binding and calibration.
- Calibration captures center/rest and full travel, previews inversion/deadzone,
  then saves identity, axis and endpoints together. Existing bindings and
  uncalibrated transforms remain unchanged until an explicit successful save.
- Added an FFB device dropdown following saved steering identity by default,
  exact explicit overrides, missing-device refusal and zero-before-switch.
- Added offline production ImGui raster fixtures at 720p/4K, input calibration
  checks and fake-ABI output-gate tests.
- Added a local install/restore package that backs up recognized Tweaks runtime
  and preserves existing settings, bindings and profiles.
- Pinned only the native output DLL/header to official toolkit v0.13.0/native
  0.6.0; retained v0.8.0 force model, profiles and encoder unchanged.

Independent pedal devices and custom camera mounts remain follow-up work.
Physical wheel/force behavior has not been accepted in game.
