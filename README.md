# Halloween Bang - ESP32 Halloween Decoration

A ESP32-based Halloween show with sound and LED effects. Can be triggered wirelessly (ESP-NOW) or locally (button press).

The show: a 1980s cartoon **ACME fuse** sparks along a 2 m LED strip towards a **ring of fire** on the floor, where a (bad) vampire stands. The fuse fizzles, nearly dies, re-ignites, the ring catches fire and builds up to a white-hot **KABOOM** - and the vampire vanishes in the flash. A purple ghost wisp drifts back along the fuse afterwards.

## Three Operation Modes

### BANGER Mode (default)
- Receives "BANG" messages via ESP-NOW from other devices
- Plays sound effects and LED animations when triggered wirelessly
- Without a working DFPlayer (not wired, no SD card, BUSY pin stuck) it still plays the full light show and strobe, just silent. Boot gives up on the DFPlayer after 3 tries (about 10 s); reboot the BANGER to get sound back after fixing it
- Use: `python3 bang.py deploy banger`
- Status: built, flashed and working on the real board

### BUTTON Mode
- Press physical button to send "BANG" messages via ESP-NOW
- Can trigger other BANGER devices wirelessly
- "BANG" is sent 3 times (`BANG_REPEATS`) in case a packet is lost; the BANGER plays one show only
- Works on its own: boots **READY** and is READY again after every show (no KILLSWITCH needed)
- Once a KILLSWITCH sends its first LOCK the button is **guarded**: LOCKED (lamp dark, presses thrown away) until the actor ARMs it. After every show it is LOCKED again (so nobody presses twice), but only as a **cooldown**: `COOLDOWN_AFTER_SHOW_MS` (30 s) later it unlocks by itself, so a lost ARM never leaves it dead. A LOCK from the actor (also during the show) holds it locked until the next ARM. A power cycle of the BUTTON unit forgets the guard.
- The arcade button lamp is a 12 V LED with a 470 ohm series resistor inside (about 20 mA), driven low-side by a 2N2222 on GPIO 7; see `docs/button_schematic.svg`
- Lamp (PWM): COUNTDOWN = flickers and gets brighter, READY = slow breathing ("press me"), SHOW = fast flicker for `WAIT_TIME_BUTTON` (16 s)
- Use: `python3 bang.py deploy button`
- Status: built, flashed and working on the real board together with the BANGER

### KILLSWITCH Mode (optional)
- A small ESP32-C3 SuperMini with one push button and one status LED, carried by the actor (was planned on an ESP32-S3 SuperMini; the firmware builds for the C3, but its pins are not yet checked for the C3: GPIO 8 is a boot-strapping pin and the on-board LED there)
- **Tap = LOCK** (BUTTON locks immediately), **hold 1 s = ARM** (BUTTON goes COUNTDOWN, after `ARM_DELAY_MS` = 10 s and a released button it is READY)
- ARM/LOCK are broadcast (no MAC needed) and sent 3x; the BUTTON reports its state (`S0`..`S3`) every second, so the LED shows: 2 quick blinks = no link, 1 blink/2 s = locked, 4 blinks/s = countdown, solid = ready, fast flicker = show
- Optional: skip it and everything works as before. Tap it once and the button is guarded.
- Guarded mode is fail-safe (a lost ARM or a reboot of the KILLSWITCH leaves the button locked), but the very first LOCK is the one that must arrive: it is sent 3x, and the LED shows READY (solid) if it did not, so check it after tapping
- Needs CPU at 80 MHz or more for Wi-Fi (the firmware sets it)
- Wiring: GPIO 8 = button to GND (internal pull-up), GPIO 7 = 330 ohm + LED to GND. See `docs/killswitch_schematic.svg` and `docs/system_overview.svg` (states and messages)
- Use: `python3 bang.py identify <port>`, add its MAC to `boards.toml`, then `python3 bang.py deploy killswitch`
- Power: the KILLSWITCH runs fine from a USB power bank, or from a LiPo on the board's `B+` / `B-` pads (see "Battery power" below)
- Status: logic tested on a PC mock only, not yet compiled/flashed on a real board (the BUTTON side is). The countdown display on the BUTTON is planned for later.

## Build Commands

`bang.py` builds, flashes and monitors the boards listed in `boards.toml` (name, MAC, chip, role). It finds each board's serial port by its MAC, so it does not matter which `/dev/ttyACM*` a board gets: native-USB ESP32-S3/C3 boards report their MAC as the USB serial number, so finding them needs no reset. Flashing goes through esptool with `--chip` from `boards.toml`, so esptool refuses a board of the wrong chip type, and afterwards the MAC that esptool read is checked again.

```
python3 bang.py list                    # ports, MACs and which board is where
python3 bang.py identify /dev/ttyACM2   # chip + MAC of a new board, prints its boards.toml entry
python3 bang.py deploy banger button    # build + flash (or: build / flash; 'all' = every board)
python3 bang.py deploy all --log        # with serial logging (-DLOG_VERBOSE)
python3 bang.py monitor banger button   # serial output of both, lines prefixed with the board name
python3 bang.py update                  # install/update the esp32 core and the DFPlayer library
```

- Builds go to `dist/<chip>/<role>/` (one per chip and role, so building one role no longer overwrites the other). `build.json` there records the flags used.
- The BUTTON and KILLSWITCH firmware get the BANGER's MAC from `boards.toml` (`-DBANGER_MAC`); the sketch falls back to the built-in MAC when it is built without `bang.py`.
- Only the bootloader (0x0), the partition table (0x8000) and the app (0x10000) are written, never the full-flash `merged.bin`.
- Opening the monitor does not reset the boards. If a board resets or is flashed, the monitor reconnects by itself.
- Extra compiler flags: `--flags='-DX -UY'` or `EXTRA_FLAGS`. `DEBUG=1` adds `-DDEBUG`. `--port` skips the MAC lookup (one board only).
- Needs Python 3.11+ (`tomllib`), pyserial, arduino-cli with the `esp32:esp32` core. esptool is taken from the installed core.
- `bash ./build.sh <command> [board|role]` still works and calls `bang.py`; the old style `EXTRA_FLAGS='-DBANGER' bash ./build.sh deploy` takes the role from the flags, and `DEV_PORT` is passed on as `--port`.

Serial logging is off unless `LOG_VERBOSE` is defined (`--log`).

## Schematics (docs/)

SVG, editable, up to date. Dashed boxes are optional or planned parts.

| File | Shows |
|------|-------|
| `docs/system_overview.svg` | the three units, the messages between them, the BUTTON states |
| `docs/banger_schematic.svg` | BANGER: ESP32-S3 Zero, LED strip, DFPlayer + speaker, optional strobe |
| `docs/button_schematic.svg` | BUTTON: SuperMini, MT3608, 2N2222 lamp driver, arcade button (redrawn from `button_schema.jpeg`) |
| `docs/killswitch_schematic.svg` | KILLSWITCH (optional): SuperMini, push button, status LED |

## LED Strip Layout

One 5 m WS2812B strip (60 LED/m = 300 LEDs) on GPIO 4, cut logically in two parts (see the `LINE_*` / `RING_*` defines in `halloween-bang.ino`):

| Part | LEDs | Index range | Length |
|------|------|-------------|--------|
| Fuse (starts at the audience / button) | 120 | 0-119 | 2.0 m |
| Ring (on the floor, closed circle) | 180 | 120-299 | 3.0 m circumference, ~0.95 m across |

The fuse enters the ring at LED 120 and LED 299 sits right next to it (keep a normal LED pitch at that joint). Put that joint on the audience side: the fire races around both arms and meets behind the vampire. To change the ring size, only change `LINE_END` and `RING_START`; everything else is computed from them.

Power: 300 LEDs at full white can pull several amps. Inject 5 V / GND at the start and ideally at the fuse/ring joint (LED 120), use at least a 5 V / 4 A supply, a 300-470 ohm resistor on the data line, and a 1000 uF electrolytic capacitor (10-16 V rated, + to 5 V) across 5 V / GND at the strip start (see `docs/banger_schematic.svg`). The 330 ohm protects the first LED, the capacitor absorbs the current steps of the white flash. A 74AHCT125 level shifter on the data line is the proper fix if the first LEDs flicker, since the ESP32 outputs 3.3 V and a 5 V WS2812B formally wants about 3.5 V. The 1 kohm in the DFPlayer RX line is optional (can reduce hiss).

## Strobe LED (BANGER)

A 5-10 W cool-white (6000-6500 K) LED module (COB or a short dense 12 V strip, behind a diffuser) is switched on its negative side by a MOSFET. Chosen switch: a ready-made **"D4184" MOSFET trigger module** (8-pack from Amazon, sold as "DC MOSFET Modules 5V-36V 400W 15A ... Dual Driver PWM"). The chips marked D4184 are AOS AOD4184(A), 40 V / 50 A N-channel, two in parallel on one output; trigger input 3.3-20 V (PWM up to 20 kHz), load supply 5-36 V. Despite "dual" it is one switched output per board, and the listing's "IRLZ44N" is just keyword stuffing. Do **not** confuse it with the ON Semi "D4184", a P-channel MOSFET + Schottky part that does not fit this circuit.

Wiring with the module (follow the silkscreen on the board; the seller pages do not name the terminals):
- ESP32 `GPIO 5` (`STROBE_PIN`) → module trigger/signal input, ESP32 GND → module trigger GND (shared with the 12 V supply GND)
- 12 V adapter → module supply input; LED branches (with their resistors, see below) → module output terminals
- Before wiring the LEDs: unpowered, measure continuity between the + input and + output terminals. About 0 ohm means the + side passes straight through and the - side is switched, which is what this circuit needs. Then power the module with the ESP32 unplugged and check the output stays off; if it flickers on, add a 10 kohm resistor from the trigger input to GND.
- At 0.9 A the module is nowhere near its limits. Sellers warn a 3.3-5 V pin may not fully open the gate at the full 15 A rating, which does not matter here.

Alternative without the module: a logic-level N-MOSFET (IRLZ44N, IRLB8721 or AO3400; not an IRF520 module, it does not switch fully at 3.3 V):
- ESP32 `GPIO 5` (`STROBE_PIN`) → 100-220 ohm → MOSFET gate, plus a 10 kohm pull-down from gate to GND (keeps the strobe off while the ESP32 boots)
- MOSFET source → GND, MOSFET drain → LED minus, LED plus → 12 V (through a series resistor for bare COB chips, see below)

LED choice:
- Suggested part choice (not yet tested on the real build): 3 of the 10-pack 3 W COB chips (DC 9-11 V, 300 mA, 6500 K white, ~9 W total, ~0.9 A from the 12 V supply). Bare chips need a **series resistor per chip** (start with 10 ohm, 2 W; measure the current with a multimeter and go down to ~6.8 ohm if it is below ~250 mA), three such branches in parallel on the MOSFET drain. Mount them on aluminium or a heatsink and put a diffuser in front. Do not run all 10 (~36 W).
- Power the strobe from its **own 12 V supply** (a 15 W adapter is plenty) and only tie its GND to the ESP32 GND. Do not run it from the 5 V USB charger: a 5 V / 2 A charger is already close to its limit during the KABOOM flash and a pulsed 1 A load would risk resets and DFPlayer noise.
- Mounting: stick the chips on an aluminium heatsink with thin thermal double-sided tape (for example the 3M 8810 series, about 0.1-0.25 mm, rated about 0.6 W/mK; ratings of no-name tape are often inflated). Clean both surfaces with isopropyl alcohol and press firmly without bubbles. The strobe only runs about 1.5 s per show, so tape is enough; for continuous use or higher power use thermal epoxy or a clamp with thermal paste. Solder the wires and resistors before sticking the chips down.
- Aim the strobe at the ring or the smoke, not at people's eyes. Flashes at 8-10 Hz are in the photosensitive-epilepsy range, so keep the burst short and put up a warning sign.

Rough 5 V budget (WS2812B ~60 mA per LED at full white, scaled by brightness): at `setBrightness(10)` the white-hot ring is about 0.4 A; at 40 it is about 1.7 A. With the ESP32, DFPlayer and speaker, a 2 A charger is fine up to roughly brightness 20; above that use a 5 V / 4 A supply.

## Battery power (KILLSWITCH, ESP32-S3 SuperMini)

These findings are for the Nologo ESP32-S3 SuperMini (Tinytronics SKU 007217) the KILLSWITCH was first planned on. The KILLSWITCH is now an ESP32-C3 SuperMini: check that board for a charger and `B+` / `B-` pads before relying on the points below (many C3 SuperMini boards have neither, so they need a power bank or an external charger/protection board). Findings:
- It has a **TP4054** single-cell lithium charger (schematic from the Tinytronics download) and `B+` / `B-` battery pads (check the polarity on the silkscreen before connecting). The TP4054 charges to 4.2 V, so only LiPo / Li-ion cells, not LiFePO4. The charge current is set by a PROG resistor (about 2 kohm = 500 mA, 10 kohm = 100 mA, from memory); the actual value on this board is **not verified**, measure it once with a multimeter in series.
- The blue LED is the **charge status**, not a low-battery warning: it blinks with no battery connected, stays on while charging, and goes out when the battery is full or when the board runs from the battery. (Per the seller's text; not measured.)
- There is **no over-discharge protection** in the charger: use a protected LiPo cell (1000-2000 mAh is plenty).
- Current draw (typical ESP32-S3 datasheet values, not measured on this board): about 90-100 mA with Wi-Fi listening all the time (the sketch sets `WiFi.setSleep(false)`, which the KILLSWITCH needs to receive the BUTTON's state), short transmit peaks of 200-300 mA. So roughly 4-5 h from 500 mAh, 8-9 h from 1000 mAh.
- Charge it while the board is idle or off: with about 100 mA of load the TP4054 may never see its end-of-charge current, so the LED stays on and the cell sits at 4.2 V.
- Not recommended: an ESP-01 (ESP8266) for the KILLSWITCH. It does ESP-NOW and has GPIO0 / GPIO2, but the sketch would need an ESP8266 port, both pins are boot-strapping pins, and a coin cell (CR2032) cannot deliver the Wi-Fi transmit peaks.

## Animations

Both animations are in `halloween-bang.ino` between `ANIMATIONS BEGIN/END`. Everything is tunable with the `#define`s at the top of that block (`FUSE_BURN_MS`, `FAKEOUT_*`, `RING_IGNITE_MS`, `BUILDUP_MIN_MS`, `KABOOM_*`).

**Animation 1 (during sound 1, ~10.3 s)**
1. **Fuse sizzle (0-5.5 s):** brown twisted rope, a white-hot spark accelerates along it with a fiery trail and crackling sparks; burnt part goes dark.
2. **Fake-out (~3.7-4.7 s):** the spark stalls at 55 %, flickers and nearly dies, then re-ignites with a big white flare (`FAKEOUT_ENABLED 0` disables it).
3. **Ring ignition (~6.5-8.3 s):** the fire spreads both ways around the ring from the fuse joint.
4. **Build-up (~8.3-10.3 s):** flicker speeds up and the ring turns white-hot. The ring stays lit until sound 1 ends.

**Animation 2 (during sound 2, ~3.8 s)**
1. **KABOOM:** white flash on the ring (the vampire disappears here) and a shockwave racing back down the fuse.
2. **Strobe burst (150-1650 ms):** a separate white strobe LED flashes about 9 times per second (40 ms on) over the slide-whistle vanish, so the vampire can step out between flashes (`STROBE_ENABLED 0` disables it).
3. **Cool down:** fireball fades through orange to dying embers.
4. **Ghost wisp:** a purple wisp drifts back along the fuse, then everything goes dark.

Total show length is about 14.5 s (10.3 s + 4.0 s, plus DFPlayer start latency).

## Audio Setup (MP3 for DFPlayer Mini)

The firmware plays two sound effects from the root of the SD card (FAT32):
- **Sound 1** (`0001.mp3`, 10.3 s): fuse sizzle and crackle, fake-out sputter and re-ignite flare, ring whoosh and fire roar, then a rising riser with accelerating cartoon time-bomb ticks, cut to silence just before the KABOOM.
- **Sound 2** (`0002.mp3`, 4.0 s): crack and sub boom, blast and falling debris, a cartoon slide-whistle "vanish" ending in a pop, and an eerie theremin-like ghost wisp with glassy bells.

Both files are synthesised by `sounds.py`, timed to the animation constants in the sketch:

```
pip install numpy scipy
python3 sounds.py        # needs ffmpeg; writes 0001.mp3 and 0002.mp3
```

If you change the animation timings in the sketch, change the matching constants at the top of `sounds.py` and run it again. `SLIDE_WHISTLE = False` removes the cartoon slide whistle.

To use your own recordings instead, convert them with:

```
python convert_for_dfplayer.py input_file.mp3 output_file.mp3
```

or with custom output name:

```
python convert_for_dfplayer.py input_file.mp3 -o 0001.mp3
```

This converts to 44.1kHz, 192kbps constant bitrate MP3 compatible with the DFPlayer Mini. Copy the files to the SD card root as `0001.mp3` and `0002.mp3`.

## Development Notes

### Board GPIO Pinouts
- **BANGER mode**: Requires **ESP32 S3 Zero** - uses GPIO 4 (LED strip DIN), GPIO 12/13 (DFPlayer TX/RX), GPIO 7 (BUSY pin), GPIO 21 (builtin LED), GPIO 8 (button input pullup)
- **KILLSWITCH mode**: **ESP32-C3 SuperMini** - GPIO 8 button, GPIO 7 status LED (pins still from the S3 SuperMini plan, not yet checked for the C3)
- **BUTTON mode**: Requires **ESP32 S3 SuperMini** - same GPIO assignments for button input and ESP-NOW communication

### Why these boards?
The firmware uses specific GPIO pins for the DFPlayer Mini UART (GPIO 12 TX, GPIO 13 RX) and LED strip (GPIO 4). The ESP32 S3 Zero and SuperMini have the correct pinout for these connections. Other ESP32 variants may have different pin mappings.

### Brightness and volume
- `strip.setBrightness(10)` keeps current draw safe but leaves few usable levels per colour, so dim parts (unburnt fuse, fade-outs) can look steppy. Raise it to ~40 once the power supply allows. Gamma (`setGamma(true)`) is off on purpose at low brightness.
- `SOUND_VOLUME` is 20 of 30. The MP3s are mastered loud; adjust on site.

## Environment Variables
- `EXTRA_FLAGS` - extra compiler flags (also `--flags`)
- `DEBUG` - set to 1 to add the `-DDEBUG` flag
- `DEV_PORT` - (`build.sh` only) use this port instead of finding it by MAC
- `ARDUINO_DIRECTORIES_DATA` - arduino-cli data directory (default: `./.arduino15` if it exists, else arduino-cli's default)
- `DEV_URLS` - board manager URL for `update`
