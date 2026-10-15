# Halloween Bang Project Reference

## Overview
ESP32-based Halloween show with sound and LED effects: an ACME-style cartoon fuse burns along an LED strip into a ring of fire (a person stands in it), builds up and goes KABOOM. Can be triggered wirelessly (ESP-NOW) or locally (button).

## Design Principle: availability beats protection
The show must always work. The killswitch is a convenience for the actor, never a requirement for the show to run. If a kid presses the button too early and the show starts, that is acceptable. A show that does not run because of a connectivity problem (lost FREE, dead killswitch, flat battery, reboot) is not. So when designing or changing BUTTON/KILLSWITCH logic:
- Every failure must end in the button working, not in it staying locked: the BUTTON works without any killswitch (boots READY) and is only LOCKED while LOCK messages keep arriving (hold-to-lock, a dead man's switch in reverse). The lock drops `LOCK_TIMEOUT_MS` (1 s) after the last LOCK, and after `LOCK_MAX_MS` (15 s) even while LOCKs still arrive (a switch left on); the BUTTON enforces both itself.
- Never add a path where a missing message, a dead unit or a reboot can leave the BUTTON locked. There is no open-ended lock and no lock state that outlives the LOCK stream.
- Sends are best effort, so each is repeated: BUTTON -> BANGER "BANG" `BANG_REPEATS` (3) times, KILLSWITCH "FREE" `KILL_REPEATS` (3) times, 60 ms apart; "LOCK" every `KILL_LOCK_PERIOD_MS` (150 ms) while held. The receiver acts on the first copy and ignores the rest: the BANGER clears `trigger_animation` after `play_something()` so copies that arrive during the show never start a second one.
- The BANGER never depends on the killswitch; only "BANG" triggers the show.
- The BANGER never depends on the DFPlayer either: the light show must run without sound. The ESP-NOW callback is registered first in `banger_setup()`; DFPlayer init is tried `DFPLAYER_INIT_TRIES` (3) times and BUSY must go HIGH within `DFPLAYER_BUSY_TIMEOUT_MS` (3 s), else `sound_ok` = 0 (lights only, `sound_is_playing()` returns 0). In the show every wait on BUSY is bounded (`SOUND_END_TIMEOUT_MS` 2 s after each animation, `BUILDUP_MAX_MS` in animation 1); hitting a bound also sets `sound_ok` = 0. BUSY uses `INPUT_PULLUP` so an unconnected wire reads "not playing". No idle re-init of the DFPlayer: reboot the BANGER to get sound back.
- No wait loop may rely on the watchdog being fed inside it: `doYIELD` only yields, `wdt_feed()` runs once per `loop()` pass.
- Re-check this principle (and the mock scenarios for it) whenever the state machine changes.

## Hardware Notes
- KILLSWITCH board: now an ESP32-C3 SuperMini (cheaper; S3 boards are kept for other things). Pins per chip in the `KILL_*_PIN` block (C3: GPIO 3 external button, GPIO 9 on-board BOOT button does the same, GPIO 8 on-board blue LED, active LOW; avoid 2/8/9 strapping, 12-17 flash, 18/19 USB, 20/21 UART0). BANGER/BUTTON have S3 pins only (`#error` on a C3 build). C3 Wi-Fi TX power is 19.5 dBm (`KILL_TX_POWER`). Lower power (8.5 dBm) was tried: on the desk it received all LOCKs, but at 7 m reception stayed between 10% and 68% at every power level, so power is not the main factor there (interference on the channel is the suspect, not checked yet). Range is still the open problem. Most C3 SuperMinis have no LiPo charger. The findings that follow are for the Nologo ESP32-S3 SuperMini (Tinytronics SKU 007217) it was first planned on: TP4054 LiPo charger, `B+`/`B-` pads, blue LED = charge status (blinks with no battery). Charge current (PROG resistor) not verified. No over-discharge protection: protected cell only. About 90-100 mA with Wi-Fi listening (sleep is off in `setup()`), so do not plan on a coin cell; ESP-01 (ESP8266) is not a drop-in (different ESP-NOW API, only GPIO0/GPIO2, boot-strapping pins).
- Strip: the 1000 uF capacitor across 5 V / GND at the strip start is required, the 330 ohm on the data line stays, a 74AHCT125 level shifter is the fix if the first LEDs flicker. The DFPlayer runs from 5 V (BANGER and EXPLOSION): its TX and BUSY lines each need 1 kohm in series (the S3/C3 pins are not 5 V tolerant), and BUSY needs no pull-down (the internal pull-up keeps idle HIGH). The 1 kohm on ESP TX -> DFPlayer RX is optional (reduces hiss). The arcade lamp is an LED with a 470 ohm series resistor inside (about 20 mA). See `docs/*_schematic.svg`.
- Do not log `WiFi.macAddress()` early in `setup()`: before the Wi-Fi interface is up it prints 00:00:00:00:00:00 (it was removed on purpose).
- Strobe chips are mounted with thin thermal double-sided tape (3M 8810 class) on aluminium; fine for the 1.5 s burst.

## Key Files

### `halloween-bang/halloween-bang.ino` (main firmware)
- **Three modes** (build flag `-DBANGER`, `-DBUTTON` or `-DKILLSWITCH`):
  - **BANGER**: Receives "BANG" via ESP-NOW → plays sounds/animations
  - **BUTTON**: Non-blocking state machine, states LOCKED / READY / SHOW. KILLSWITCH is optional: boots READY. READY → (press: unicast "BANG") SHOW → (`WAIT_TIME_BUTTON` 16 s) READY, or LOCKED if the killswitch is held. READY → LOCKED while LOCKs arrive (`lock_seen_ms`, set in the ESP-NOW callback, cleared by FREE; compare with signed `int32_t` maths). LOCKED → READY on FREE, `LOCK_TIMEOUT_MS` without LOCK, or `LOCK_MAX_MS` (`lock_expired`: LOCKs ignored until the stream stops), once the button is released (a queued press is cleared). A LOCK during SHOW does not stop the show. Lamp = PWM on `LED_12V_PIN` (GPIO 7), patterns in `lamp_level()`. Sends nothing back to the KILLSWITCH: the lamp is the actor's feedback.
  - **KILLSWITCH** (optional): ESP32-C3 SuperMini, GPIO 3 button + on-board BOOT (GPIO 9), on-board LED (GPIO 8). Held (push button or switch) = broadcast "LOCK" every 150 ms; release = "FREE" 3x. After `LOCK_MAX_MS` held it stops sending LOCK, sends FREE and blinks fast until released. Send-only: LED on while held, fast blink = expired, 1 blink at power-up; it does not show the BUTTON state (`LOCK_MAX_MS` is `#ifndef`, so a test build can pass `--flags=-DLOCK_MAX_MS=...`). Schematics: `docs/killswitch_schematic.svg`, `docs/system_overview.svg`.
- Messages (block `MESSAGES BEGIN/END`, strict length+memcmp matching): "BANG", "LOCK", "FREE", "SND1", "SND2". "SND1"/"SND2" go from the BANGER to the optional EXPLOSION box just before it starts 0001.mp3 / 0002.mp3 itself; the box plays the same file, ignores repeats for `SND_DEDUPE_MS` (1 s), and nothing waits for it. CPU must run ≥80 MHz for ESP-NOW (`setup_cpu_speed(80)`).
- BANGER (ESP32-S3 Zero) and BUTTON (ESP32-S3 SuperMini, soldered in, stays S3) are built, flashed and verified working on the real boards (`python3 bang.py deploy banger button`). KILLSWITCH (ESP32-C3 SuperMini) is flashed with the hold-to-lock firmware. The planned countdown 8-segment display belonged to the removed ARM/COUNTDOWN design; rethink it before building it.
- Serial logging needs `-DLOG_VERBOSE` (`bang.py ... --log`).
- Controls 300 WS2812B LEDs (5 m, 60 LED/m) via `EdSoftLED` library
  - Fuse: `LINE_START`..`LINE_END` = LEDs 0-119 (2 m, starts at the audience/button)
  - Ring: `RING_START`..`RING_END` = LEDs 120-299 (closed circle, ~0.95 m across)
  - Ring size is changed only via `LINE_END` / `RING_START`; all animation maths derives from them
- DFRobot DFPlayer Mini for MP3 playback (`0001.mp3` fuse/ignition/build-up, `0002.mp3` kaboom)
- ESP-NOW wireless communication
- Schematics are SVG in `docs/` (`system_overview`, `banger_schematic`, `button_schematic`, `killswitch_schematic`); dashed boxes mean optional/planned. Keep them in sync when pins, parts or states change.
- Watchdog timer (60s timeout), fed only from `loop()` (`wdt_feed()`). Note `trigger_panic = false` and the ISR handler only logs, so a timeout does not reset the board yet.
- LED animations (block between `ANIMATIONS BEGIN/END`, tuned with `#define`s: `FUSE_BURN_MS`, `FAKEOUT_*`, `RING_IGNITE_MS`, `BUILDUP_MIN_MS`, `KABOOM_*`):
  - `run_ledstrip_animation_1()`: fuse spark with fake-out → ring ignites both ways → white-hot build-up. Does not clear at the end; it waits until sound 1 finishes.
  - `run_ledstrip_animation_2()`: white flash + shockwave, embers, purple ghost wisp, then clear. Also drives the strobe LED via `strobe_update(ms)`: 1.5 s burst at ~9 Hz starting 150 ms in (`STROBE_START_MS`, `STROBE_DURATION_MS`, `STROBE_PERIOD_MS`, `STROBE_ON_MS`; `STROBE_ENABLED 0` disables it). The strobe pin is forced LOW again when the animation ends.
- Strobe LED: `STROBE_PIN` = GPIO 5 (BANGER) → trigger input of an AOD4184A ("D4184") MOSFET trigger module (or the gate of a logic-level N-MOSFET with 100-220 ohm series and 10 kohm pull-down), strobe LED on its own 12 V supply with shared GND (3x 3 W 9-11 V COB chips, each with its own ~6.8-10 ohm series resistor, ~0.9 A total). GPIO 5 is a "safe" pin on the S3 Zero; GPIO 4 strip, 7 BUSY, 8 button, 12/13 DFPlayer are taken.
- Show flow (`play_something()`): play sound 1 + animation 1 → wait for BUSY pin low→high (max `SOUND_END_TIMEOUT_MS`) → play sound 2 + animation 2 → wait for sound end (same bound). About 14.5 s in total; without a DFPlayer (`sound_ok` = 0) the sounds are skipped and the animations run on their own timers (~14.1 s).

### `bang.py` + `boards.toml` - build / flash / monitor
- `boards.toml`: one entry per physical board: `mac`, `chip` (`esp32s3` | `esp32c3`), `role`. It is the only place MACs live: the BUTTON/KILLSWITCH get the BANGER MAC as `-DBANGER_MAC` (the sketch's `#ifndef BANGER_MAC` default is a fallback for builds without `bang.py`).
- The port is found by MAC from the USB serial number (native USB-Serial/JTAG, no reset); esptool (from the installed core) flashes with `--chip` and the MAC it reads is checked. Output in `dist/<chip>/<role>/`, build cache in `.build/`.
- Flashes bootloader 0x0, partitions 0x8000, app 0x10000 only (no `merged.bin`, no `boot_app0`), so data in other flash regions survives a flash.
- Monitor: pyserial, opens with RTS cleared before DTR (pyserial's own order resets the board on USB-Serial/JTAG).
- `build.sh` is only a wrapper around `bang.py`.
- Planned next: one build per chip with the role stored in a small flash partition written by `bang.py flash`.

### `EdSoftLED.h/cpp` - LED strip driver
- Supports WS2812 and SK6812 strips
- Uses ESP32 RMT peripheral (4 memory blocks, 2 on chips with only 2 TX channels like the C3)
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

## Build Artifacts (in `dist/<chip>/<role>/`)
- `halloween-bang.ino.bin` - Firmware (~940 KB on S3)
- `halloween-bang.ino.elf` - ELF binary (9MB)
- `halloween-bang.ino.map` - Map file
- `halloween-bang.ino.merged.bin` - Merged binary (4MB)
- Bootloader and partitions binaries

## Operation
- Modes (roles): `BANGER` (wireless trigger, lights + sound), `BUTTON` (local button), optional `KILLSWITCH` (hold-to-lock), optional `EXPLOSION` (sound only: plays the BANGER's sounds in a second place; build flag `-DEXPLOSION`, chip per `boards.toml`, the BANGER gets its MAC as `-DEXPLOSION_MAC` only when the box is in `boards.toml` with a MAC, so rebuild the BANGER after adding it)
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
- `LOG()` never blocks: on the USB serial (HWCDC) build `setup()` sets `setTxTimeoutMs(0)` and a 2 KB TX buffer (`LOG_TX_BUFFER`), each line is one write, and a line that does not fit is dropped whole (PC plugged in but no monitor reading). Without this, HWCDC `write()` waits up to ~2 s per call and stalls the show. No USB host (power bank) = logging skipped.
