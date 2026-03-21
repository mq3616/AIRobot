# AIRobot Project Memory

This repository uses the following default workflow for any code changes:

1. If the AIRobot Console GUI is running, close it before uploading firmware.
2. After code changes are finished, build and upload the firmware to the board.
3. If the upload succeeds, reopen AIRobot Console by running:
   `powershell -ExecutionPolicy Bypass -File scripts\open_serial_console_gui.ps1`

Additional project notes:

- Treat this workflow as the default for all AIRobot tasks unless the user explicitly says not to upload or not to reopen the console.
- Prefer the existing AIRobot Console GUI instead of separate serial monitor shells.
- Keep the serial/flash workflow centered on the current PlatformIO environment in this repo.
