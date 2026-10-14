# Halloween Bang Project Reference

## Overview
ESP32-based Halloween show with sound and LED effects: an ACME-style cartoon fuse burns along an LED strip into a ring of fire (a person stands in it), builds up and goes KABOOM. Can be triggered wirelessly (ESP-NOW) or locally (button).

## Design Principle: availability beats protection
The show must always work. The killswitch/guard is a convenience for the actor, never a requirement for the show to run. If a kid presses the button too early and the show starts, that is acceptable. A show that does not run because of a connectivity problem (lost ARM, dead killswitch, flat battery, reboot) is not. So when designing or changing BUTTON/KILLSWITCH logic:
- Every failure must end in the button working, not in it staying locked: the BUTTON works without any killswitch (boots READY), and a guarded lock is only a cooldown (`COOLDOWN_AFTER_SHOW_MS`, 30 s) that expires by itself.
- Never add a path where a missing message, a dead unit or a reboot can leave the BUTTON locked forever. The only open-ended lock is an explicit LOCK from the actor (`hold_lock`), released by ARM.
- Sends are best effort, so each is repeated: BUTTON -> BANGER "BANG" `BANG_REPEATS` (3) times, KILLSWITCH ARM/LOCK `KILL_REPEATS` (3) times, 60 ms apart. The receiver acts on the first copy and ignores the rest: the BANGER clears `trigger_animation` after `play_something()` so copies that arrive during the show never start a second one.
- The BANGER never depends on the killswitch; only "BANG" triggers the show.
- Re-check this principle (and the mock scenarios for it) whenever the state machine changes.

## Key Files

### `halloween-bang/halloween-bang.ino` (main firmware)
- **Three modes** (build flag `-DBANGER`, `-DBUTTON` or `-DKILLSWITCH`):
  - **BANGER**: Receives "BANG" via ESP-NOW → plays sounds/animations
  - **BUTTON**: Non-blocking state machine. KILLSWITCH is optional: boots READY, `guarded`=0, back to READY after each show. The first LOCK sets `guarded` (also remembered during SHOW; forgotten on reboot); then LOCKED → (ARM) COUNTDOWN → (`ARM_DELAY_MS` 10 s + button released) READY → (press: unicast "BANG") SHOW → (`WAIT_TIME_BUTTON` 16 s) LOCKED (or COUNTDOWN if `AUTO_REARM_AFTER_SHOW 1`). After a show the lock is a cooldown: `COOLDOWN_AFTER_SHOW_MS` (30 s, 0 = off) later it goes READY by itself unless the actor sent LOCK (`hold_lock`, cleared by ARM). LOCK sends COUNTDOWN/READY to LOCKED; ARM is ignored in READY/SHOW. Lamp = PWM on `LED_12V_PIN` (GPIO 7), patterns in `lamp_level()`. Sends its state "S0".."S3" every `STATUS_PERIOD_MS`.
  - **KILLSWITCH** (optional): SuperMini, GPIO 8 button, GPIO 7 LED. Tap = "LOCK", hold `ARM_HOLD_MS` (1 s) = "ARM", both broadcast `KILL_REPEATS` (3) times. LED shows the BUTTON state (no link after `STATUS_TIMEOUT_MS`). Schematics: `docs/killswitch_schematic.svg`, `docs/system_overview.svg`.
- Messages (block `MESSAGES BEGIN/END`, strict length+memcmp matching): "BANG", "ARM", "LOCK", "S0".."S3". CPU must run ≥80 MHz for ESP-NOW (`setup_cpu_speed(80)`).
- The BUTTON/KILLSWITCH logic was only tested on a host mock, not compiled for the ESP32 yet. Countdown 8-segment display is planned, not implemented.
- Controls 300 WS2812B LEDs (5 m, 60 LED/m) via `EdSoftLED` library
  - Fuse: `LINE_START`..`LINE_END` = LEDs 0-119 (2 m, starts at the audience/button)
  - Ring: `RING_START`..`RING_END` = LEDs 120-299 (closed circle, ~0.95 m across)
  - Ring size is changed only via `LINE_END` / `RING_START`; all animation maths derives from them
- DFRobot DFPlayer Mini for MP3 playback (`0001.mp3` fuse/ignition/build-up, `0002.mp3` kaboom)
- ESP-NOW wireless communication
- Schematics are SVG in `docs/` (`system_overview`, `banger_schematic`, `button_schematic`, `killswitch_schematic`); dashed boxes mean optional/planned. Keep them in sync when pins, parts or states change.
- Watchdog timer (60s timeout)
- LED animations (block between `ANIMATIONS BEGIN/END`, tuned with `#define`s: `FUSE_BURN_MS`, `FAKEOUT_*`, `RING_IGNITE_MS`, `BUILDUP_MIN_MS`, `KABOOM_*`):
  - `run_ledstrip_animation_1()`: fuse spark with fake-out → ring ignites both ways → white-hot build-up. Does not clear at the end; it waits until sound 1 finishes.
  - `run_ledstrip_animation_2()`: white flash + shockwave, embers, purple ghost wisp, then clear. Also drives the strobe LED via `strobe_update(ms)`: 1.5 s burst at ~9 Hz starting 150 ms in (`STROBE_START_MS`, `STROBE_DURATION_MS`, `STROBE_PERIOD_MS`, `STROBE_ON_MS`; `STROBE_ENABLED 0` disables it). The strobe pin is forced LOW again when the animation ends.
- Strobe LED: `STROBE_PIN` = GPIO 5 (BANGER) → trigger input of an AOD4184A ("D4184") MOSFET trigger module (or the gate of a logic-level N-MOSFET with 100-220 ohm series and 10 kohm pull-down), strobe LED on its own 12 V supply with shared GND (3x 3 W 9-11 V COB chips, each with its own ~6.8-10 ohm series resistor, ~0.9 A total). GPIO 5 is a "safe" pin on the S3 Zero; GPIO 4 strip, 7 BUSY, 8 button, 12/13 DFPlayer are taken.
- Show flow (`play_something()`): play sound 1 + animation 1 → wait for BUSY pin low→high → play sound 2 + animation 2 → wait for sound end. About 14.5 s in total.

### `EdSoftLED.h/cpp` - LED strip driver
- Supports WS2812 and SK6812 strips
- Uses ESP32 RMT peripheral
- Gamma correction, brightness control
- `begin()` claims RMT channel (call once; `setup()` also calls `rmtInit`, remove that one if flicker or "init sender failed" appears)
- Colour quirk: `getRed`/`getGreen` and the strip-type naming are swapped and cancel out. Use `strip.Color(r,g,b)` with `setPixelColor(i, uint32_t)`; the 5-argument `setPixelColor(i,r,g,b,w)` gives wrong colours.

### `sounds.py`
- Synthesises `0001.mp3` and `0002.mp3` with numpy/scipy + ffmpeg (no samples needed)
- Mirrors the animation timing constants in the sketch (`FUSE_BURN_S`, `FAKEOUT_*`, `RING_IGNITE_S`, `BUILDUP_S`, `KABOOM_TOTAL`). **If you change timings in the sketch, change them here too and re-run `python3 sounds.py`.**
- Sound 1 length must match the sketch's cross time + `RING_IGNITE_MS` + `BUILDUP_MIN_MS` (~10.3 s) so the build-up ends exactly when the audio cuts out.
- `SLIDE_WHISTLE = False` removes the cartoon vanish whistle from sound 2

### `convert_for_dfplayer.py`
- FFmpeg-based audio transcoder for your own recordings
- Converts to 44.1kHz, 192kbps MP3 for DFPlayer Mini

### `0001.mp3`, `0002.mp3`
- Generated by `sounds.py`; copy to the root of the SD card (FAT32)

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
- `BUTTON` mode sends "BANG" messages when button pressed. Presses made while the lamp blinks (during the show) are discarded: after `WAIT_TIME_BUTTON` the code waits for the button to be released, debounces, and clears the queued interrupt flag (`button_was_pressed`) before the lamp comes back on.
- Both modes play sound effects and LED animations
- `WAIT_TIME_BUTTON` (16 s) must stay longer than the whole show (~14.5 s) so the button lamp only lights again when the show is over. Re-check it whenever animation or sound lengths change.

## Workflow Note
- Do not immediately commit and push unless explicitly asked
- Review all changes with `git status` and `git diff` first
- Commit messages should be concise and match repo style
- Only commit when ready; use `git add -p` for selective staging if needed

## Logging
- Use `LOG(...)` macro instead of `Serial.print()` / `Serial.println()` for all debugging output
- `LOG()` automatically prepends timestamps in `[HH:MM:SS][info]:` format
- Converts multiple `Serial.print` calls into a single `LOG()` call with variadic arguments
- Example: `LOG("Button pressed on pin %d", BUTTON_PIN)` replaces `Serial.print("Button pressed on pin: "); Serial.println(BUTTON_PIN)`
