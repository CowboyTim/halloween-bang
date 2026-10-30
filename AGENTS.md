# Halloween Bang Project Reference

## Overview
ESP32-based Halloween show with sound and LED effects: an ACME-style cartoon fuse burns along an LED strip into a ring of fire (a person stands in it), builds up and goes KABOOM. Can be triggered wirelessly (ESP-NOW) or locally (button).

## Design Principle: availability beats protection
The show must always work. The killswitch is a convenience for the actor, never a requirement for the show to run. If a kid presses the button too early and the show starts, that is acceptable. A show that does not run because of a connectivity problem (lost FREE, dead killswitch, flat battery, reboot) is not. So when designing or changing BUTTON/KILLSWITCH logic:
- Every failure must end in the button working, not in it staying locked: the BUTTON works without any killswitch (boots READY) and is only LOCKED while LOCK messages keep arriving (hold-to-lock, a dead man's switch in reverse). The lock drops `LOCK_TIMEOUT_MS` (1 s) after the last LOCK, and after `LOCK_MAX_MS` (30 s) even while LOCKs still arrive (a switch left on); the BUTTON enforces both itself.
- Never add a path where a missing message, a dead unit or a reboot can leave the BUTTON locked. There is no open-ended lock and no lock state that outlives the LOCK stream.
- Sends are best effort, so each is repeated: BUTTON -> BANGER "BANG" `BANG_REPEATS` (3) times, KILLSWITCH "FREE" `KILL_REPEATS` (3) times, 60 ms apart; "LOCK" every `KILL_LOCK_PERIOD_MS` (150 ms) while held. The receiver acts on the first copy and ignores the rest: the BANGER clears `trigger_animation` after `play_something()` so copies that arrive during the show never start a second one.
- The BANGER never depends on the killswitch; only "BANG" triggers the show.
- The BUTTON also has a wired killswitch input (GPIO 1, `wired_kill_held()`, switch to GND on a cable of up to ~10 m), OR'ed with the radio lock: LOW for `WIRED_KILL_ENGAGE_MS` (100 ms) to count, HIGH for `WIRED_KILL_RELEASE_MS` (30 ms) to release, and the same `LOCK_MAX_MS` expiry, so a shorted/stuck wire cannot block the button for good and a cut wire (pull-up HIGH) leaves it working. Keep the switch normally open: a normally-closed loop would lock the button when the wire is cut. The filter is tested off-board (extract the text between `WIRED KILL BEGIN/END` and run it against simulated signals).
- The KILLSWITCH -> BUTTON link is ESP-NOW only (the KILLSWITCH's 433 MHz STX882 is not fitted and was removed from its build on 2026-10-30; the BUTTON's SRX882 receiver path still takes either link). Either link is enough, both only feed the lock timer, so a lost link or a dead module can only shorten a lock, never extend it. Do not make the BUTTON depend on the 433 MHz receiver being present.
- Add-on boxes (EXPLOSION, STROBO) are best-effort listeners: the BANGER sends to them without waiting for an answer, so a missing, unpowered or broken box never changes the show. Check on the bench that sends to an absent peer do not delay it.
- The BANGER has no DFPlayer in the default build (`-DBANGER_DFPLAYER` adds one): the EXPLOSION boxes play the sounds and nothing in the show waits for them. The light show never depends on sound. The ESP-NOW callback is registered first in `banger_setup()`. With `-DBANGER_DFPLAYER`: DFPlayer init is tried `DFPLAYER_INIT_TRIES` (3) times and BUSY must go HIGH within `DFPLAYER_BUSY_TIMEOUT_MS` (3 s), else `sound_ok` = 0 (lights only, `sound_is_playing()` returns 0). In the show every wait on BUSY is bounded (`SOUND_END_TIMEOUT_MS` 2 s after each animation, `BUILDUP_MAX_MS` in animation 1); hitting a bound also sets `sound_ok` = 0. BUSY uses `INPUT_PULLUP` so an unconnected wire reads "not playing". No idle re-init of the DFPlayer: reboot the BANGER to get sound back. Without the flag all DFPlayer code is left out of the build.
- No wait loop may rely on the watchdog being fed inside it: `doYIELD` only yields, `wdt_feed()` runs once per `loop()` pass.
- Re-check this principle (and the mock scenarios for it) whenever the state machine changes.

## Hardware Notes
- KILLSWITCH board: now an ESP32-C3 SuperMini (cheaper; S3 boards are kept for other things). Pins per chip in the `KILL_*_PIN` block (C3: GPIO 3 external button, GPIO 9 on-board BOOT button does the same, GPIO 8 on-board blue LED, active LOW; avoid 2/8/9 strapping, 12-17 flash, 18/19 USB, 20/21 UART0). BANGER/BUTTON have S3 pins only (`#error` on a C3 build). C3 Wi-Fi TX power is 19.5 dBm (`KILL_TX_POWER`). Lower power (8.5 dBm) was tried: on the desk it received all LOCKs, but at 7 m reception stayed between 10% and 68% at every power level, so power is not the main factor there (interference on the channel is the suspect, not checked yet). Range is still the open problem. Most C3 SuperMinis have no LiPo charger. The findings that follow are for the Nologo ESP32-S3 SuperMini (Tinytronics SKU 007217) it was first planned on: TP4054 LiPo charger, `B+`/`B-` pads, blue LED = charge status (blinks with no battery). Charge current (PROG resistor) not verified. No over-discharge protection: protected cell only. About 90-100 mA with Wi-Fi listening (sleep is off in `setup()`), so do not plan on a coin cell; ESP-01 (ESP8266) is not a drop-in (different ESP-NOW API, only GPIO0/GPIO2, boot-strapping pins).
- Strip: a capacitor across 5 V / GND at the strip start is required (1000 uF recommended; 470 uF / 16 V is in use and fine at `setBrightness(10)`, about 0.7 A worst case), the 330 ohm on the data line stays, a 74AHCT125 level shifter is the fix if the first LEDs flicker. The DFPlayer runs from 5 V (BANGER and EXPLOSION), with 470 uF / 16 V + 100 nF decoupling across its VCC/GND pins: its TX and BUSY lines each need 1 kohm in series (the S3/C3 pins are not 5 V tolerant), and BUSY needs no pull-down (the internal pull-up keeps idle HIGH). The 1 kohm on ESP TX -> DFPlayer RX is optional (reduces hiss). The arcade lamp is an LED with a 470 ohm series resistor inside (about 20 mA). See `docs/*_schematic.svg`.
- Do not log `WiFi.macAddress()` early in `setup()`: before the Wi-Fi interface is up it prints 00:00:00:00:00:00 (it was removed on purpose).
- Strobe chips are mounted with thin thermal double-sided tape (3M 8810 class) on aluminium; fine for the 150 ms flash.

## Key Files

### `halloween-bang/halloween-bang.ino` (main firmware)
- **Three modes** (build flag `-DBANGER`, `-DBUTTON` or `-DKILLSWITCH`):
  - **BANGER**: Receives "BANG" via ESP-NOW → plays sounds/animations. Status LED = the S3 Zero's WS2812 on `BUILTIN_LED` (GPIO 21), bit-banged in `ws2812_write()` (interrupts off ~30 us per write; `neopixelWrite()`/RMT cannot be used, the strip owns it) and updated from `myYIELD()` so it also runs inside the show's loops: dim green = ready, dim blue = cooldown (`banger_cooling()`, BANGs dropped), orange flicker = `animation_is_running`, yellow blink = `!sound_ok`. Written only when the colour changes
  - **BUTTON**: Non-blocking state machine, states LOCKED / READY / SHOW. KILLSWITCH is optional: boots READY. READY → (press: unicast "BANG") SHOW → (`WAIT_TIME_BUTTON` 16 s) READY, or LOCKED if the killswitch is held. READY → LOCKED while LOCKs arrive (`lock_seen_ms`, set in the ESP-NOW callback, cleared by FREE; compare with signed `int32_t` maths). LOCKED → READY on FREE, `LOCK_TIMEOUT_MS` without LOCK, or `LOCK_MAX_MS` (`lock_expired`: LOCKs ignored until the stream stops), once the button is released (a queued press is cleared). A LOCK during SHOW does not stop the show. Lamp = PWM on `LED_12V_PIN` (GPIO 7), patterns in `lamp_level()`. Sends nothing back to the KILLSWITCH: the lamp is the actor's feedback.
  - **KILLSWITCH** (optional): ESP32-C3 SuperMini, GPIO 3 button + on-board BOOT (GPIO 9), on-board LED (GPIO 8), `KILL_EXT_LED_PIN` GPIO 10 = LED in the lock switch (3-6 V type, active HIGH, mirrors the on-board LED). Held (push button or switch) = "LOCK" every 150 ms; release = "FREE" 3x (`KILL_FREE_GAP_MS` 100 ms apart). Every message goes out over ESP-NOW broadcast only; the BUTTON takes LOCK/FREE from either link (`apply_lock_msg()`, SRX882 DATA on GPIO 6 via 1 kohm, polled in `rf433_poll()`), both feed `lock_seen_ms`, and `lock_links` records which delivered (logged on release). RadioHead configures GPIO 4/5/6 on the BUTTON only; `USE_RF433` is 1 only in BUTTON builds. After `LOCK_MAX_MS` held it stops sending LOCK, sends FREE and blinks fast until released. Send-only: LED on while held, fast blink = expired, 1 blink at power-up; it does not show the BUTTON state (`LOCK_MAX_MS` is `#ifndef`, so a test build can pass `--flags=-DLOCK_MAX_MS=...`). Schematics: `docs/killswitch_schematic.svg`, `docs/system_overview.svg`.
- Decisions that are NOT to be redone: see `TODO.md` ("Won't do": role-in-flash, BANGER LED/sound split, ESP-01, ARM/COUNTDOWN).
- Messages (block `MESSAGES BEGIN/END`, strict length+memcmp matching): "BANG", "LOCK", "FREE", "SND1", "SND2", "STRB". "STRB" goes from the BANGER to the optional STROBO box(es) just before its animation 2 (KABOOM) starts; each box fires one short burst of flashes (`STROBO_FLASHES`). "SND1"/"SND2" go from the BANGER to the optional EXPLOSION box just before it starts 0001.mp3 / 0002.mp3 itself; the box plays the same file, ignores repeats for `SND_DEDUPE_MS` (1 s), and nothing waits for it. CPU must run ≥80 MHz for ESP-NOW (`setup_cpu_speed(80)`).
- BANGER (ESP32-S3 Zero) and BUTTON (ESP32-S3 SuperMini, soldered in, stays S3) are built, flashed and verified working on the real boards (`python3 bang.py deploy banger button`). KILLSWITCH (ESP32-C3 SuperMini) is flashed with the hold-to-lock firmware. The planned countdown 8-segment display belonged to the removed ARM/COUNTDOWN design; rethink it before building it.
- Serial logging needs `-DLOG_VERBOSE` (`bang.py ... --log`).
- Test build (`-DTEST_SERIAL`, always with `--log`): reads one command per line on the USB serial (`status`, `fire`, `lock`, `free`; `fire` on the BUTTON is the queued press, on the BANGER the BANG). Used only by `tools/bench/run_tests.py` (see `docs/test_plan.md`). Never flash a test build to a board that runs a show; production builds (no `--log`, no `TEST_SERIAL`) do not read the serial port at all.
- Controls 600 WS2812B LEDs (two 5 m strips in one chain, 60 LED/m) via `EdSoftLED` library
  - Fuse: `LINE_START`..`LINE_END` = LEDs 0-419 (7 m, starts at the audience/button)
  - Ring: `RING_START`..`RING_END` = LEDs 420-599 (closed circle, ~0.95 m across, the last 180 LEDs)
  - Ring size is changed only via `LINE_END` / `RING_START`; all animation maths derives from them. The animation times do not depend on the fuse length (the sound is fixed): `FUSE_SCALE` (fuse length / 120, the length it was designed on) scales the trail, the spark spread and the speed of the shockwave and the ghost wisp, so a longer fuse just burns faster. The ring still starts at `fuse_cross_ms()`, unchanged
- DFRobot DFPlayer Mini for MP3 playback (`0001.mp3` fuse/ignition/build-up, `0002.mp3` kaboom): in the EXPLOSION boxes; on the BANGER only with `-DBANGER_DFPLAYER`
- ESP-NOW wireless communication
- Schematics are SVG in `docs/` (`system_overview`, `banger_schematic`, `button_schematic`, `killswitch_schematic`); dashed boxes mean optional/planned. Keep them in sync when pins, parts or states change.
- Watchdog timer (60s timeout), fed only from `loop()` (`wdt_feed()`). `trigger_panic = true`: a stuck `loop()` makes the IDF panic (backtrace of the stuck code) and reboot after 60 s. The user handler `esp_task_wdt_isr_user_handler()` runs in interrupt context, so it only uses `esp_rom_printf` (no `LOG()`, no Serial, no `esp_restart()`: those crashed into an Interrupt WDT panic and hid the message). The timeout must stay longer than the longest `loop()` pass (a whole show is ~14.5 s inside one pass).
- LED animations (block between `ANIMATIONS BEGIN/END`, tuned with `#define`s: `FUSE_BURN_MS`, `FAKEOUT_*`, `RING_IGNITE_MS`, `BUILDUP_MIN_MS`, `KABOOM_*`):
  - `run_ledstrip_animation_1()`: fuse spark with fake-out → ring ignites both ways → white-hot build-up. Does not clear at the end; it waits until sound 1 finishes.
  - `run_ledstrip_animation_2()`: white flash + shockwave, embers, purple ghost wisp, then clear. The BANGER has no strobe of its own: the KABOOM strobe is the STROBO box's burst of flashes (on `STRB`).
- Strobe LED (STROBO box only): `STROBE_PIN` = GPIO 5 → trigger input of an AOD4184A ("D4184") MOSFET trigger module (or the gate of a logic-level N-MOSFET with 100-220 ohm series and 10 kohm pull-down), strobe LED on its own 12 V supply with shared GND (3x 3 W 9-11 V COB chips, each with its own ~6.8-10 ohm series resistor, ~0.9 A total). The strobe is in the STROBO box, so GPIO 5 is not used on the BANGER.
- DFPlayer library hazard: the library's ACK wait (`sendStack()` with ACK on: `while (_isSending) waitAvailable()`) spins forever when noise on the RX line keeps `available()` true and no DFPlayer clears `_isSending`. It froze an EXPLOSION box with no DFPlayer in the bench test (the watchdog rebooted it after 60 s). `dfplayer_answers()` therefore detects the DFPlayer with a raw "query status" (0x42) and reads the answer itself, bounded by `DFPLAYER_BUSY_TIMEOUT_MS`; only then `begin(stream, false, false)` (ACK off, no reset) and every later command is sent without an ACK wait. Never re-enable ACK mode.
- Show flow (`play_something()`): `SND1` to the boxes + sound 1 + animation 1 → wait for sound 1 end → `SND2` + sound 2 + `STRB` + animation 2 → wait for sound 2 end. Default BANGER: no DFPlayer, so nothing waits and the show is the animations' own timers, about 14 s. With `-DBANGER_DFPLAYER` the BUSY waits (`SOUND_END_TIMEOUT_MS`) apply and the show is about 14.5 s.

### `bang.py` + `boards.toml` - build / flash / monitor
- `boards.toml`: one entry per physical board: `mac`, `chip` (`esp32s3` | `esp32c3`), `role`. It is the only place MACs live: the BUTTON/KILLSWITCH get the BANGER MAC as `-DBANGER_MAC` (the sketch's `#ifndef BANGER_MAC` default is a fallback for builds without `bang.py`).
- The port is found by MAC from the USB serial number (native USB-Serial/JTAG, no reset); esptool (from the installed core) flashes with `--chip` and the MAC it reads is checked. Output in `dist/<chip>/<role>/`, build cache in `.build/`.
- Several boards per role are supported (a spare BUTTON, several EXPLOSION/STROBO boxes): `resolve()` turns a selector into boards; a role selector means all boards of the role, and a name that is both a board and a role (`button`) means the whole role once the role has more than one board. `present()` skips optional boards that are not plugged in (a role with several boards, or `all`) and fails for required ones (a board named exactly, or the only board of a role); builds are shared per chip+role.
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
- Modes (roles): `BANGER` (wireless trigger, lights; sound goes to the EXPLOSION boxes, `-DBANGER_DFPLAYER` adds its own DFPlayer), `BUTTON` (local button), optional `KILLSWITCH` (hold-to-lock), optional `STROBO` (strobe only: on `STRB` fires a burst of `STROBO_FLASHES` (6) flashes of `STROBO_ON_MS` (80 ms) with `STROBO_OFF_MS` (120 ms) between, hard stop after `STROBO_MAX_BURST_MS` (2 s), at `STROBO_BRIGHTNESS` (PWM, 255 = full); ignores a new `STRB` within `STRB_MIN_GAP_MS` (5 s) of a burst; strobe pin GPIO 5, C3 LED GPIO 8; `docs/strobo_schematic.svg`), optional `EXPLOSION` (sound only: plays the BANGER's sounds in a second place; build flag `-DEXPLOSION`, chip per `boards.toml`, the BANGER gets the MACs of ALL EXPLOSION and STROBO boards in `boards.toml` that have one, as `-D<ROLE>_COUNT=n -D<ROLE>_MACS=<flat bytes>`, so rebuild the BANGER after adding or removing a box; any number of boxes of each role is supported)
- `BANGER` mode receives "BANG" messages via ESP-NOW from other devices
- `BUTTON` mode sends "BANG" messages when button pressed. A press counts only when the pin is LOW for `PRESS_MIN_MS` (30 ms) after it was HIGH (released) for `RELEASE_MIN_MS` (50 ms) (`button_armed`, polled in `do_button_loop()`); the interrupt only counts edges, and edges that are no press are logged as glitches. So a noise spike, a loose wire or a chattering / stuck contact cannot start shows on its own, and a button still held after a show must be let go first. Before this, any falling edge in READY was a press: a stuck or chattering arcade button replayed the show every 16 s until someone pressed it properly (seen at a test, 2026-10-28). Presses during LOCKED / SHOW are thrown away. The BANGER logs the MAC of the BUTTON that sent BANG and counts BANGs (`bang_rx`), so a second BUTTON with the show firmware (the spare) shows up in the log.
- Both modes play sound effects and LED animations
- `WAIT_TIME_BUTTON` (16 s) must stay longer than the whole show (~14.5 s) so the button lamp only lights again when the show is over. Re-check it whenever animation or sound lengths change. After it comes `SHOW_COOLDOWN_MS` (60 s, `#ifndef` so a test build can shorten it): the BUTTON stays in SHOW with the lamp dark until `WAIT_TIME_BUTTON + SHOW_COOLDOWN_MS`, and the BANGER drops every BANG for `SHOW_COOLDOWN_MS` after its show ends, then clears its trigger ("fresh start"). The BUTTON's total must stay longer than the BANGER's show + cooldown, or a press after the BUTTON's wait would be dropped. Added after shows replayed back to back at a test (2026-10-28, most likely a stuck arcade contact): whatever happens at the button, shows are at least 76 s apart. A button held `BUTTON_STUCK_MS` (2 s) after its press makes the lamp flicker (logged as "stuck?").

## Workflow Note
- Do not immediately commit and push unless explicitly asked
- Review all changes with `git status` and `git diff` first
- Commit messages should be concise and match repo style
- Only commit when ready; use `git add -p` for selective staging if needed
- End commit messages with `Co-Authored-By: Claude <noreply@anthropic.com>`. Never put a model name (Claude Opus / Sonnet / Haiku, or a model ID) in commit messages, PR titles or bodies, code comments, or anything else pushed to the repo

## Logging
- Use `LOG(...)` macro instead of `Serial.print()` / `Serial.println()` for all debugging output
- `LOG()` automatically prepends timestamps in `[HH:MM:SS][info]:` format
- Converts multiple `Serial.print` calls into a single `LOG()` call with variadic arguments
- Example: `LOG("Button pressed on pin %d", BUTTON_PIN)` replaces `Serial.print("Button pressed on pin: "); Serial.println(BUTTON_PIN)`
- `LOG()` never blocks: on the USB serial (HWCDC) build `setup()` sets `setTxTimeoutMs(0)` and a 2 KB TX buffer (`LOG_TX_BUFFER`), each line is one write, and a line that does not fit is dropped whole (PC plugged in but no monitor reading). Without this, HWCDC `write()` waits up to ~2 s per call and stalls the show. No USB host (power bank) = logging skipped.
