# Install OutRun wheel settings

This is an x86 development candidate for the Windows PC version of OutRun 2006
Coast 2 Coast. No game files are included. Physical wheel acceptance remains
pending; the existing force tune has not changed.

1. Exit the game normally.
2. Extract this package and run **Install.bat**.
3. Paste the folder containing **OR2006C2C.EXE** when asked. The installer checks
   the package, backs up the previous runtime, preserves existing settings and
   bindings, then verifies the installed files. It does not launch the game.
4. Start the game normally and press **F6**. Setup explains how to enable wheel
   controls if needed. Enabling that input route requires one normal restart.
5. In **Controls**, Bind or Calibrate Steering, Throttle and Brake. Capture
   center/rest, move only that control through its travel, then preview the
   direction and deadzone. **Save calibration** commits the complete binding.
   Cancel or a disconnected device retains the previous binding.
6. In **FFB**, the device defaults to the saved steering wheel. You can choose an
   explicit device from the dropdown. FFB remains Off until you enable it.
   **F8** or **Stop FFB** saves Off. Strength is available in Simple; detailed
   tuning is in Advanced. The force signal still needs attended validation.

Independent pedal devices, handbrake output, custom Bonnet/Bumper mounts and
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
