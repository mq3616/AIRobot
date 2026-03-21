# AIRobot

AIRobot is an ESP32-S3-based embedded AI robot project that combines voice interaction, facial/status display, music playback, and a desktop control console into one practical prototype workflow.

## Background

The project is designed for a hybrid model: lightweight local hardware plus cloud AI services.

Instead of building a purely software assistant, AIRobot focuses on a complete device-side interaction loop:

- the board handles audio capture, playback, serial control, display output, and local state
- the cloud side handles higher-cost capabilities such as chat and text-to-speech
- the desktop side provides a visual control console for flashing, log monitoring, and command sending

The goal is not to build a huge general-purpose platform first. The goal is to make a robot that can already speak, play, react, and be iterated on quickly.

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

## Technical Stack

- MCU: ESP32-S3 DevKitC-1
- Firmware framework: Arduino
- Build system: PlatformIO
- Display: Adafruit SSD1306 + RoboEyes
- Audio: I2S input/output pipeline
- Desktop tool: Python + Tkinter + pyserial

## Repository Layout

- [src](/d:/Projects/AIRobot/src)
- [include](/d:/Projects/AIRobot/include)
- [scripts](/d:/Projects/AIRobot/scripts)
- [assets](/d:/Projects/AIRobot/assets)
- [docs](/d:/Projects/AIRobot/docs)

## Related Docs

- Serial console GUI: [docs/serial-console-gui.md](/d:/Projects/AIRobot/docs/serial-console-gui.md)
- Bailian chat integration: [docs/bailian-chat.md](/d:/Projects/AIRobot/docs/bailian-chat.md)
- Bailian TTS integration: [docs/bailian-tts.md](/d:/Projects/AIRobot/docs/bailian-tts.md)

## Current Positioning

AIRobot currently works well as:

- an AI voice robot prototype
- an embedded speech interaction experiment
- an ESP32-S3 example project for audio, serial control, and desktop tooling
- a foundation for future physical robot features

## Notes

- Local secrets, serial preferences, and temporary debug artifacts are intentionally excluded from the repository
- The project is still evolving quickly, including firmware behavior, songs, desktop tooling, and timbre tuning
- For the Chinese version, see [README.md](/d:/Projects/AIRobot/README.md)
