# AIRobot Serial Console GUI

This desktop tool keeps serial monitoring, command sending, and firmware upload in one window.

## Launch

Use PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\open_serial_console_gui.ps1
```

Or run Python directly:

```powershell
python .\scripts\serial_console_gui.py
```

## What It Does

- Lists available serial ports
- Connects and streams board logs in-place
- Sends typed commands such as `help`, `ask hello`, or `say hello`
- Uploads firmware through PlatformIO
- Closes the in-app serial connection before upload
- Stops leftover external PlatformIO monitor processes on the same port
- Reconnects to the board automatically after a successful upload if enabled

## Notes

- The GUI uses system `python` for `tkinter`
- The script automatically reuses `pyserial` from `%USERPROFILE%\.platformio`
- Default PlatformIO environment is `esp32-s3-devkit`
- Default baud rate is `115200`

## Recommended Flow

1. Launch the GUI
2. Pick the board port
3. Click `Connect`
4. Send commands from the input box or quick buttons
5. Click `Upload Firmware` when you want to flash new firmware
6. Keep `Reconnect after upload` enabled for the smoothest loop
