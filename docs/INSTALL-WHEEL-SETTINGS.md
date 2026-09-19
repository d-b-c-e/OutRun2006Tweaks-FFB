# Install OutRun wheel settings

This is an x86 development candidate for the Windows PC version of OutRun 2006
Coast 2 Coast. No game files are included. Physical wheel acceptance remains
pending; the existing force tune has not changed.

1. Exit the game normally.
2. Extract this package and run **Install.bat**.
3. Paste the folder containing **OR2006C2C.EXE** when asked. The installer checks
   the package, backs up the previous runtime, preserves existing settings and
   bindings, then verifies the installed files. It does not launch the game.
4. Start **OR2006C2C.EXE from its own folder**, or use a shortcut whose
   **Start in** folder is the game folder. This game loads assets relative to
   its working directory; starting the executable from another directory can
   show blank frames. Press **F6**. Setup explains how to enable wheel
   controls if needed. Enabling that input route requires one normal restart.
5. In **Controls**, Bind or Calibrate Steering, Throttle and Brake. Capture
   center/rest, move only that control through its travel, then preview the
   direction and deadzone. **Save calibration** commits the complete binding.
   Cancel or a disconnected device retains the previous binding.
   For a separate USB pedal set, choose its device from the dropdown in Throttle
   or Brake's Bind flow. Each pedal saves its own exact device identity; a missing
   device gives neutral input and is never silently replaced by another.
   For a shifter or button box, expand its group in Simple Controls and choose
   Bind on the desired action. Select the device, press/release its button, then
   Save binding. The whole group uses that device. H-pattern simulates bounded
   shifts through the native sequential gearbox.
6. In **FFB**, the device defaults to the saved steering wheel. You can choose an
   explicit device from the dropdown. FFB remains Off until you enable it.
   **F8** or **Stop FFB** saves Off. Strength is available in Simple; detailed
   tuning is in Advanced. The force signal still needs attended validation.

Settings shortcuts in Simple Controls can rebind Settings and Stop FFB to
unmodified F3-F12 keys (F11 is reserved for tools). F1/F2 stay with the game.
Help shows the actual saved shortcuts; the panel and font scale automatically
with the viewport, with a user UI-scale adjustment available in Help.

Native menus use arrows, Enter and Esc; first use asks you to create a licence.
The installed PC manual documents these controls.

Handbrake output, custom Bonnet/Bumper mounts and
camera adjustment keys are not implemented by this adapter. The native Change
camera button is bindable. Telemetry is optional; its page explains receiver
setup and distinguishes sending from confirmed receipt.

The installer never overwrites an existing INI, binding file or force profile.
It adds the shipped template/profile only if absent. Updates replace just
`dinput8.dll` and `WheelFfb.dll`; do not copy the template over your settings.

To undo an update, close the game and run the following from the extracted
package, using the exact backup path printed during installation:

```powershell
.\Install.ps1 -Action Restore -GameDirectory 'your game folder' -BackupDirectory 'printed backup folder'
```

Restore reinstates the previous runtime (or removes a DLL that was absent) and
keeps your current settings. It refuses to overwrite runtime files changed
since that install. Backups live in `.wheel-settings-backups` in the game folder.
Keep them until you are satisfied with the update. No logs are uploaded.

The package manifest identifies the exact source and DLL hashes. The native
output DLL/header use official toolkit v0.13.0/native 0.6.0 for strict device
selection; model/profile/encoder files retain the v0.8.0 baseline. Component
provenance is in `provenance/`.
