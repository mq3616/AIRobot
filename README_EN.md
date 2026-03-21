# AIRobot

[中文](./README.md) | [English](./README_EN.md)

AIRobot is an ESP32-S3-based embedded AI robot project that combines voice interaction, facial/status display, music playback, and a desktop control console into one practical prototype workflow.

## Background

The project follows a hybrid model: lightweight local hardware plus cloud AI services.

Instead of building a purely software assistant, AIRobot focuses on a complete device-side interaction loop:

- the board handles audio capture, playback, serial control, display output, and local state
- the cloud side handles higher-cost capabilities such as chat and text-to-speech
- the desktop side provides a visual control console for flashing, log monitoring, and command sending

The goal is not to build a huge platform first. The goal is to make a robot that can already speak, play, react, and be iterated on quickly.

## Project Overview

AIRobot currently includes:

- an `ESP32-S3 + Arduino + PlatformIO` firmware project
- serial-command-based control for speech, music, and status
- Bailian chat and TTS integration
- OLED facial/status rendering
- a continuously tuned music playback system for small speakers
- a dark-themed desktop serial console called `AIRobot Console`

Typical supported interactions include:

- sending `ask <text>` to get a chat response and speak it aloud
- sending `say <text>` to play TTS directly
- playing built-in melodies through serial commands
- switching between piano and music-box timbres
- changing playback parameters such as volume and accompaniment while music is playing
- flashing firmware, watching logs, and sending commands from the desktop GUI

## Current Hardware List

The current AIRobot setup uses:

- `ESP32-S3 DevKitC-1-N8`
- `INMP441` I2S microphone
- `MAX98357A` I2S amplifier
- `0.96"` `128x64` I2C OLED display
- small speaker
- USB data cable

The default pin mapping is defined in [include/app_config.h](./include/app_config.h):

- microphone: `GPIO4 / GPIO5 / GPIO6`
- OLED: `GPIO8 / GPIO9`
- amplifier: `GPIO7 / GPIO15 / GPIO16`

## Quick Start

### 1. Prepare the environment

- install `Python`
- install `PlatformIO`
- connect the development board to your computer

### 2. Configure WiFi and API key

Real secrets are not committed to the repository. These local-only files are already ignored:

- `include/app_secrets.h`
- `scripts/bailian.local.ps1`
- `.serial_console_gui.json`

The recommended setup is environment-variable-based injection. The build helper
[scripts/inject_secrets.py](./scripts/inject_secrets.py) automatically reads:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `OPENAI_API_KEY`

PowerShell example:

```powershell
$env:WIFI_SSID="your-ssid"
$env:WIFI_PASSWORD="your-password"
$env:OPENAI_API_KEY="sk-..."
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit
```

If you prefer a local fallback, copy
[include/app_secrets.example.h](./include/app_secrets.example.h)
to `include/app_secrets.h` and fill in your local values. That file is ignored by Git and will not be pushed.

### 3. Build the project

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit
```

## Flashing

### Via PlatformIO CLI

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32-s3-devkit -t upload --upload-port COM3
```

### Via AIRobot Console

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\open_serial_console_gui.ps1
```

The GUI supports:

- automatic serial connection
- command sending
- log viewing
- closing the serial connection before upload
- reopening the console after a successful flash

More details are available in [docs/serial-console-gui.md](./docs/serial-console-gui.md).

## Serial Commands

Common serial commands include:

- `help`: show help
- `wifi`: print WiFi status
- `mem`: print memory status
- `profile`: print the current user / robot profile
- `beep`: play a test tone
- `ask <text>`: call chat and automatically speak the reply
- `say <text>`: call TTS directly
- `volume`: print the current playback volume
- `volume <0-100>`: set the playback volume
- `accomp`: print the current music-box accompaniment status
- `accomp on`: enable music-box accompaniment
- `accomp off`: disable music-box accompaniment
- `melody canon musicbox`
- `melody tori musicbox`
- `melody truth musicbox`
- `melody juebieshu musicbox`
- `melody qifeng musicbox`
- `melody sinian musicbox`
- `melody stop`

For the full runtime command flow, see [src/main.cpp](./src/main.cpp).

## Technical Stack

- MCU: ESP32-S3 DevKitC-1
- Firmware framework: Arduino
- Build system: PlatformIO
- Display: Adafruit SSD1306 + RoboEyes
- Audio: I2S input/output pipeline
- Desktop tool: Python + Tkinter + pyserial

## Repository Layout

- [src](./src)
- [include](./include)
- [scripts](./scripts)
- [assets](./assets)
- [docs](./docs)

## Related Docs

- Serial console GUI: [docs/serial-console-gui.md](./docs/serial-console-gui.md)
- Bailian chat integration: [docs/bailian-chat.md](./docs/bailian-chat.md)
- Bailian TTS integration: [docs/bailian-tts.md](./docs/bailian-tts.md)

## Current Positioning

AIRobot currently works well as:

- an AI voice robot prototype
- an embedded speech interaction experiment
- an ESP32-S3 example project for audio, serial control, and desktop tooling
- a foundation for future physical robot features

## Notes

- Local secrets, serial preferences, and temporary debug artifacts are intentionally excluded from the repository
- The project is still evolving quickly, including firmware behavior, songs, desktop tooling, and timbre tuning
- For the Chinese version, see [README.md](./README.md)
