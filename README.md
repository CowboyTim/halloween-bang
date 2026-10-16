# Halloween Bang - ESP32 Halloween Decoration

A ESP32-based Halloween show with sound and LED effects. Can be triggered wirelessly (ESP-NOW) or locally (button press).

The show: a 1980s cartoon **ACME fuse** sparks along a 2 m LED strip towards a **ring of fire** on the floor, where a (bad) vampire stands. The fuse fizzles, nearly dies, re-ignites, the ring catches fire and builds up to a white-hot **KABOOM** - and the vampire vanishes in the flash. A purple ghost wisp drifts back along the fuse afterwards.

## Three Operation Modes

### BANGER Mode (default)
- Receives "BANG" messages via ESP-NOW from other devices
- Plays sound effects and LED animations when triggered wirelessly
- Without a working DFPlayer (not wired, no SD card, BUSY pin stuck) it still plays the full light show and strobe, just silent. Boot gives up on the DFPlayer after 3 tries (about 10 s); reboot the BANGER to get sound back after fixing it
- Status LED: the S3 Zero's built-in RGB LED (a WS2812 on GPIO 21, bit-banged because the strip owns the RMT peripheral) shows dim green when ready, flickers orange while the show runs, and blinks yellow while the DFPlayer is missing
- Use: `python3 bang.py deploy banger`
- Status: built, flashed and working on the real board

### BUTTON Mode
- Press physical button to send "BANG" messages via ESP-NOW
- Can trigger other BANGER devices wirelessly
- "BANG" is sent 3 times (`BANG_REPEATS`) in case a packet is lost; the BANGER plays one show only
- Works on its own: boots **READY** and is READY again after every show (no KILLSWITCH needed)
- Only while the actor holds the KILLSWITCH down is it **LOCKED** (lamp dark, presses thrown away). Release = READY at once. The lock also ends by itself 1 s after the KILLSWITCH stops sending (lost, out of range, flat battery) and after 15 s at most (`LOCK_MAX_MS`), even if the KILLSWITCH is still held
- LOCK and FREE arrive over ESP-NOW and over 433 MHz (SRX882 on GPIO 6); the log line on release says which links delivered them (`lock came via: ESP-NOW 433 MHz`). Without the SRX882 wired, the 433 MHz path simply hears nothing and ESP-NOW works as before
- The arcade button lamp is a 12 V LED with a 470 ohm series resistor inside (about 20 mA), driven low-side by a 2N2222 on GPIO 7; see `docs/button_schematic.svg`
- Lamp (PWM): LOCKED = dark, READY = slow breathing ("press me"), SHOW = fast flicker for `WAIT_TIME_BUTTON` (16 s)
- Use: `python3 bang.py deploy button`
- Status: built, flashed and working on the real board together with the BANGER

### KILLSWITCH Mode (optional)
- A small ESP32-C3 SuperMini with one push button, carried by the actor. The on-board BOOT button works as the push button too, so a bare board is enough to try it
- **Hold = the BUTTON is locked, release = it works again at once.** A push button or a switch both work
- While held it broadcasts "LOCK" every 150 ms (no MAC needed); on release it sends "FREE" 3x. The BUTTON stays locked only while LOCKs keep arriving, so a lost message, a reset or a flat battery unlocks it within 1 s
- A lock lasts at most 15 s (`LOCK_MAX_MS`), even if the switch is left on: then the KILLSWITCH LED blinks fast; release it (and hold again) to lock again
- **Two radio links, either one is enough:** the KILLSWITCH sends every LOCK and FREE over ESP-NOW and over a 433 MHz ASK link (STX882 on the KILLSWITCH, SRX882 on the BUTTON). Range test: ESP-NOW got 10-68% at 7 m; 433 MHz got 100% at 4 m, at 7 m through a wall and at 10 m through two walls (`tools/rf433_test/`). Both links feed the same lock timer on the BUTTON, so a lost link can only shorten a lock, never extend it. FREE is sent when the 433 MHz radio is idle (a packet takes about 70 ms on the air), 3 copies 100 ms apart
- 433 MHz wiring: KILLSWITCH: STX882 DATA on GPIO 4, VCC on the 3.3 V pin (tested; the product page gives no supply range), GND, a straight 17 cm wire as antenna. BUTTON: SRX882 DATA through 1 kohm on GPIO 6 (its DATA swings at its 5 V supply, the S3 pins are not 5 V tolerant), VCC 5 V, GND, a 17 cm antenna wire. RadioHead also sets GPIO 4 and 5 on both boards (unused), so keep them free; the lamp pin GPIO 7 is not touched. See `docs/killswitch_schematic.svg` and `docs/button_schematic.svg`
- The KILLSWITCH only sends. Its on-board blue LED: on while held, fast blink = lock expired, 1 blink at power-up. Whether the button is locked, the actor sees on the BUTTON's own lamp (dark = locked)
- Optional: skip it and everything works as before
- Needs CPU at 80 MHz or more for Wi-Fi (the firmware sets it)
- Wiring: GPIO 3 = button to GND (internal pull-up), nothing else. See `docs/killswitch_schematic.svg` and `docs/system_overview.svg` (states and messages)
- Use: `python3 bang.py identify <port>`, add its MAC to `boards.toml`, then `python3 bang.py deploy killswitch`
- The C3 sends at 19.5 dBm (`KILL_TX_POWER`). Lower power (8.5 dBm) was tried; at 7 m reception hardly changed with power, so it was kept at the higher level
- Power: a USB power bank. Most C3 SuperMini boards have no LiPo charger or `B+` / `B-` pads (see "Battery power" below)
- Status: flashed on the ESP32-C3 SuperMini, tested with the on-board BOOT button

### EXPLOSION Mode (optional sound box)
- A second, sound-only box that plays the same `0001.mp3` (fuse) and `0002.mp3` (KABOOM) as the BANGER, in another place (for example farther away, or with a bigger speaker)
- The BANGER tells it when each sound starts (`SND1`, `SND2`, sent just before the BANGER plays the sound). The BANGER still plays both sounds itself, and it does not wait for the box, so the show runs the same if the box is off, out of range or broken
- Repeated `SND` copies within 1 s are ignored
- Needs its own DFPlayer, speaker, 5 V supply and an SD card with the same two files
- Put 1 kΩ in series on each DFPlayer -> C3 line (TX and BUSY) and on C3 -> DFPlayer RX. The C3 pins are not 5 V tolerant. Do not put a pull-down divider on BUSY: it needs the C3's internal pull-up to read idle high
- Decoupling at the DFPlayer: a 470 uF / 16 V electrolytic (+ to 5 V) and a 100 nF ceramic across its VCC and GND pins, as close to the pins as possible (see the schematic)
- Power the DFPlayer from **5 V**, not from the C3's 3.3 V regulator. The C3's Wi-Fi transmit peaks draw from that regulator too; a 3 W speaker on the same rail makes it sag, which can reset the C3 or distort the sound. Use the SuperMini's 5 V pin (USB) or a 5 V supply, with the grounds joined.
- To add it: `python3 bang.py identify <port>`, put the MAC in `boards.toml` under `[boards.explosion]`, `python3 bang.py deploy explosion`, then `python3 bang.py deploy banger` (the BANGER needs the box's MAC at build time)
- Several boxes are possible: list each one in `boards.toml` (`[boards.explosion]`, `[boards.explosion2]`, ...). The BANGER gets all of their MACs, so rebuild it after adding or removing a box
- To turn it off: unpower it. No rebuild needed
- Built-in LED (C3 SuperMini, GPIO 8): blinks while the DFPlayer is missing, and searches for it every 5 s until it is found. Lit solid once the DFPlayer is found and the box is ready, and flickers while a sound plays. The search runs in `loop()`, so the watchdog is fed between tries
- Wiring: `docs/explosion_schematic.svg`. The C3 pins are TX GPIO 5, RX GPIO 6, BUSY GPIO 3 (UART1)
- Status: working on the bench with the DFPlayer on 5 V. The 1 kΩ values and the BUSY idle level are not measured yet

### STROBO Mode (optional strobe box)
- An add-on box with its own ESP32 that flashes the KABOOM strobe, for a strobe the BANGER cannot be cabled to. The BANGER's own strobe output still works; the box is extra
- The BANGER sends `STRB` when its KABOOM animation starts. The box then runs the same burst as the BANGER's strobe (starts 150 ms in, 1.5 s, about 9 flashes/s, 40 ms on), with the same `STROBE_*` constants
- Safety: the burst ends by itself and the pin goes LOW; a new `STRB` within 5 s of a burst is ignored, so repeats or a glitch cannot keep it strobing; it boots with the strobe off
- Hardware: strobe pin GPIO 5 -> 220 ohm -> gate of a logic-level N-MOSFET (10 kohm gate to GND), or a D4184 trigger module; LEDs on their own supply (5 V or 12 V) with a series resistor, GND shared. The built-in LED (C3, GPIO 8) is lit when ready and flickers during the burst. See `docs/strobo_schematic.svg`; the LED values are chosen once the LEDs are known
- To add it: `python3 bang.py identify <port>`, put the MAC in `boards.toml` under `[boards.strobo]`, `python3 bang.py deploy strobo`, then `python3 bang.py deploy banger`. Several boxes are possible, like EXPLOSION
- Aim it away from people's eyes; flashes at 8-10 Hz are in the photosensitive-epilepsy range (see the strobe notes below)
- Status: code and schematic written, not yet tested on hardware

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

- Spares and several boxes: a role can have several boards in `boards.toml` (for example a second BUTTON, `[boards.button_spare]`, a template is in the file). Selecting the role (`deploy button`) builds once and flashes whichever of its boards are plugged in, and prints which it skipped; naming one board (`deploy button_spare`) requires that board to be connected. `all` flashes everything that is plugged in
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
| `docs/killswitch_schematic.svg` | KILLSWITCH (optional): C3 SuperMini, push button (on-board BOOT button and LED) |
| `docs/strobo_schematic.svg` | STROBO (optional): C3 SuperMini, MOSFET, LEDs on their own supply |
| `docs/explosion_schematic.svg` | EXPLOSION (optional): C3 SuperMini, DFPlayer Mini on 5 V, 3 W speaker, 1 kΩ on the DFPlayer lines |

## LED Strip Layout

One 5 m WS2812B strip (60 LED/m = 300 LEDs) on GPIO 4, cut logically in two parts (see the `LINE_*` / `RING_*` defines in `halloween-bang.ino`):

| Part | LEDs | Index range | Length |
|------|------|-------------|--------|
| Fuse (starts at the audience / button) | 120 | 0-119 | 2.0 m |
| Ring (on the floor, closed circle) | 180 | 120-299 | 3.0 m circumference, ~0.95 m across |

The fuse enters the ring at LED 120 and LED 299 sits right next to it (keep a normal LED pitch at that joint). Put that joint on the audience side: the fire races around both arms and meets behind the vampire. To change the ring size, only change `LINE_END` and `RING_START`; everything else is computed from them.

Power: 300 LEDs at full white can pull several amps. Inject 5 V / GND at the start and ideally at the fuse/ring joint (LED 120), use at least a 5 V / 4 A supply, a 300-470 ohm resistor on the data line, and a 1000 uF electrolytic capacitor (10-16 V rated, + to 5 V) across 5 V / GND at the strip start (see `docs/banger_schematic.svg`). The 330 ohm protects the first LED, the capacitor absorbs the current steps of the white flash. A 74AHCT125 level shifter on the data line is the proper fix if the first LEDs flicker, since the ESP32 outputs 3.3 V and a 5 V WS2812B formally wants about 3.5 V. The DFPlayer runs from 5 V, so its TX and BUSY lines each need 1 kohm in series (required; the ESP pins are not 5 V tolerant). No pull-down on BUSY. The 1 kohm on ESP TX -> DFPlayer RX is optional (can reduce hiss). Put 470 uF / 16 V (+ to 5 V) and 100 nF across the DFPlayer's VCC and GND pins, right at the pins (see `docs/banger_schematic.svg`).

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

### Preparing the SD card (16 GB)

The same two files go on the BANGER's card and on the EXPLOSION box's card. `sd.txt` shows the partition layout of a 16 GB card that works: one primary partition, MBR (DOS) table, starting at sector 2048, type `b` (W95 FAT32). The card reports about 14.8 GiB.

Formatting erases the whole card. Check the device name first: the card should show about 14.9 GB and no other disk should match it.

**Linux** (replace `/dev/sdX` with your card, e.g. `/dev/sdb`; check with `lsblk`):
```
sudo umount /dev/sdX?                  # if it was auto-mounted (an error here is fine)
sudo fdisk /dev/sdX
#   o      new empty DOS (MBR) partition table
#   n p 1  primary partition 1, Enter, Enter (use the whole card)
#   t b    type W95 FAT32, as in sd.txt
#   w      write and quit
sudo partprobe /dev/sdX
sudo mkfs.vfat -F 32 -n BANG /dev/sdX1
```

**Windows:** in Explorer right-click the card, Format, file system **FAT32**, allocation unit size default, quick format. Windows can format a 16 GB card as FAT32 (its own limit is 32 GB).

**macOS:** Disk Utility, select the card (not the top-level entry with the name only), Erase, format **MS-DOS (FAT)**, scheme **Master Boot Record**, name `BANG`.

Then copy the sounds:

1. Copy `0001.mp3` first, then `0002.mp3`, into the **root** of the card (not a folder).
2. Keep nothing else on the card. The DFPlayer finds tracks 1 and 2 by their position on the card, so the card should hold exactly these two files.
3. Remove the hidden files that the computer adds. On macOS these are `.Spotlight-V100`, `.Trashes` and `._*` files; on Windows `System Volume Information`. For example, on macOS:
   ```
   rm -rf /Volumes/BANG/.Spotlight-V100 /Volumes/BANG/.Trashes /Volumes/BANG/._* /Volumes/BANG/.fseventsd
   ```
4. Eject the card properly before taking it out.

Power off the board before putting the card in or taking it out. The card is read at boot; if the DFPlayer is not found, check the card in this order: format (FAT32, MBR), file names (`0001.mp3`, `0002.mp3` in the root), no other files, card seated, then reboot.

A 16 GB card is much bigger than needed: the two files together are about 360 KB. Any card up to 32 GB works.

To test a card: boot the board with logging on (`python3 bang.py monitor banger`). The log should show `DFPlayer Mini ready` and `DFPlayer BUSY HIGH, continue`. A show should then print `Playing File: 1` and `Playing File: 2`.

## Development Notes

### Board GPIO Pinouts
- **BANGER mode**: Requires **ESP32 S3 Zero** - uses GPIO 4 (LED strip DIN), GPIO 12/13 (DFPlayer TX/RX), GPIO 7 (BUSY pin), GPIO 21 (builtin LED), GPIO 8 (button input pullup)
- **KILLSWITCH mode**: **ESP32-C3 SuperMini** - GPIO 3 button (and the on-board BOOT button, GPIO 9), on-board blue LED (GPIO 8). Avoid GPIO 2/8/9 (strapping), 12-17 (flash), 18/19 (USB), 20/21 (serial). An S3 build keeps GPIO 8 button / GPIO 7 LED
- **BUTTON mode**: Requires **ESP32 S3 SuperMini** - same GPIO assignments for button input and ESP-NOW communication

### Why these boards?
The firmware uses specific GPIO pins for the DFPlayer Mini UART (GPIO 12 TX, GPIO 13 RX) and LED strip (GPIO 4). The ESP32 S3 Zero and SuperMini have the correct pinout for these connections. Other ESP32 variants may have different pin mappings.

### Brightness and volume
- `strip.setBrightness(10)` keeps current draw safe but leaves few usable levels per colour, so dim parts (unburnt fuse, fade-outs) can look steppy. Raise it to ~40 once the power supply allows. Gamma (`setGamma(true)`) is off on purpose at low brightness.
- `SOUND_VOLUME` is set per role in the sketch: BANGER 20 of 30, EXPLOSION box 10 of 30 (lowered while its DFPlayer ran from the C3's 3.3 V pin). The MP3s are mastered loud; adjust on site.
- The DFPlayer library has one volume command (`0x06`, 0-30) and an on/off switch for the line-out (DAC, `0x1A`), so there is no separate line-out level in the protocol. Whether the line-out follows the volume setting is not tested on this module yet. If it does not need a separate level, feed the line-out into a small amplifier with its own volume knob and keep the DFPlayer at a fixed level

## Environment Variables
- `EXTRA_FLAGS` - extra compiler flags (also `--flags`)
- `DEBUG` - set to 1 to add the `-DDEBUG` flag
- `DEV_PORT` - (`build.sh` only) use this port instead of finding it by MAC
- `ARDUINO_DIRECTORIES_DATA` - arduino-cli data directory (default: `./.arduino15` if it exists, else arduino-cli's default)
- `DEV_URLS` - board manager URL for `update`
