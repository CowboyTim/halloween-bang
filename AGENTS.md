# Halloween Bang Project Reference

## Overview
ESP32-based Halloween decoration with sound and LED effects. Can be triggered wirelessly (ESP-NOW) or locally (button).

## Key Files

### `halloween-bang.ino` (main firmware)
- **Two modes** controlled by `BANGER` define:
  - **BANGER**: Receives "BANG" via ESP-NOW → plays sounds/animations
  - **BUTTON**: Press button → sends "BANG" via ESP-NOW
- Controls 300 WS2812 RGB LEDs via `EdSoftLED` library
- DFRobot DFPlayer Mini for MP3 playback (whoosh + kaboom)
- ESP-NOW wireless communication
- Watchdog timer (60s timeout)
- LED animations: sweeping spark + erratic ring glow

### `EdSoftLED.h/cpp` - LED strip driver
- Supports WS2812 and SK6812 strips
- Uses ESP32 RMT peripheral
- Gamma correction, brightness control
- `begin()` claims RMT channel (call once)

### `convert_for_dfplayer.py`
- FFmpeg-based audio transcoder
- Converts to 44.1kHz, 192kbps MP3 for DFPlayer Mini

### `partitions.csv`
- ESP32 partition table (OTA, SPIFFS, NVS, storage)

## Build Artifacts (in `dist/`)
- `halloween-bang.ino.bin` - Firmware (903KB)
- `halloween-bang.ino.elf` - ELF binary (9MB)
- `halloween-bang.ino.map` - Map file
- `halloween-bang.ino.merged.bin` - Merged binary (4MB)
- Bootloader and partitions binaries

## Operation
- Two modes: `BANGER` (wireless trigger) or `BUTTON` (local button)
- `BANGER` mode receives "BANG" messages via ESP-NOW from other devices
- `BUTTON` mode sends "BANG" messages when button pressed
- Both modes play sound effects and LED animations

## Workflow Note
- Do not immediately commit and push unless explicitly asked
- Review all changes with `git status` and `git diff` first
- Commit messages should be concise and match repo style
- Only commit when ready; use `git add -p` for selective staging if needed