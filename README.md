# Halloween Bang - ESP32 Halloween Decoration

A ESP32-based Halloween show with sound and LED effects. Can be triggered wirelessly (ESP-NOW) or locally (button press).

The show: a 1980s cartoon **ACME fuse** sparks along a 2 m LED strip towards a **ring of fire** on the floor, where a (bad) vampire stands. The fuse fizzles, nearly dies, re-ignites, the ring catches fire and builds up to a white-hot **KABOOM** - and the vampire vanishes in the flash. A purple ghost wisp drifts back along the fuse afterwards.

## Two Operation Modes

### BANGER Mode (default)
- Receives "BANG" messages via ESP-NOW from other devices
- Plays sound effects and LED animations when triggered wirelessly
- Use: `EXTRA_FLAGS=-DBANGER DEV_PORT=/dev/ttyACM0 bash ./build.sh deploy`

### BUTTON Mode
- Press physical button to send "BANG" messages via ESP-NOW
- Can trigger other BANGER devices wirelessly
- The button lamp blinks for `WAIT_TIME_BUTTON` (16 s) while the show runs, then lights up again
- Use: `EXTRA_FLAGS=-DBUTTON DEV_PORT=/dev/ttyACM1 bash ./build.sh deploy`

## Build Commands

```
# BANGER mode (wireless trigger)
EXTRA_FLAGS=-DBANGER DEV_PORT=/dev/ttyACM0 bash ./build.sh deploy

# BUTTON mode (local button)
EXTRA_FLAGS=-DBUTTON DEV_PORT=/dev/ttyACM1 bash ./build.sh deploy
```

## LED Strip Layout

One 5 m WS2812B strip (60 LED/m = 300 LEDs) on GPIO 4, cut logically in two parts (see the `LINE_*` / `RING_*` defines in `halloween-bang.ino`):

| Part | LEDs | Index range | Length |
|------|------|-------------|--------|
| Fuse (starts at the audience / button) | 120 | 0-119 | 2.0 m |
| Ring (on the floor, closed circle) | 180 | 120-299 | 3.0 m circumference, ~0.95 m across |

The fuse enters the ring at LED 120 and LED 299 sits right next to it (keep a normal LED pitch at that joint). Put that joint on the audience side: the fire races around both arms and meets behind the vampire. To change the ring size, only change `LINE_END` and `RING_START`; everything else is computed from them.

Power: 300 LEDs at full white can pull several amps. Inject 5 V / GND at the start and ideally at the fuse/ring joint (LED 120), use at least a 5 V / 4 A supply, and a 300-470 ohm resistor on the data line.

## Animations

Both animations are in `halloween-bang.ino` between `ANIMATIONS BEGIN/END`. Everything is tunable with the `#define`s at the top of that block (`FUSE_BURN_MS`, `FAKEOUT_*`, `RING_IGNITE_MS`, `BUILDUP_MIN_MS`, `KABOOM_*`).

**Animation 1 (during sound 1, ~10.3 s)**
1. **Fuse sizzle (0-5.5 s):** brown twisted rope, a white-hot spark accelerates along it with a fiery trail and crackling sparks; burnt part goes dark.
2. **Fake-out (~3.7-4.7 s):** the spark stalls at 55 %, flickers and nearly dies, then re-ignites with a big white flare (`FAKEOUT_ENABLED 0` disables it).
3. **Ring ignition (~6.5-8.3 s):** the fire spreads both ways around the ring from the fuse joint.
4. **Build-up (~8.3-10.3 s):** flicker speeds up and the ring turns white-hot. The ring stays lit until sound 1 ends.

**Animation 2 (during sound 2, ~3.8 s)**
1. **KABOOM:** white flash on the ring (the vampire disappears here) and a shockwave racing back down the fuse.
2. **Cool down:** fireball fades through orange to dying embers.
3. **Ghost wisp:** a purple wisp drifts back along the fuse, then everything goes dark.

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
- **BUTTON mode**: Requires **ESP32 S3 SuperMini** - same GPIO assignments for button input and ESP-NOW communication

### Why these boards?
The firmware uses specific GPIO pins for the DFPlayer Mini UART (GPIO 12 TX, GPIO 13 RX) and LED strip (GPIO 4). The ESP32 S3 Zero and SuperMini have the correct pinout for these connections. Other ESP32 variants may have different pin mappings.

### Brightness and volume
- `strip.setBrightness(10)` keeps current draw safe but leaves few usable levels per colour, so dim parts (unburnt fuse, fade-outs) can look steppy. Raise it to ~40 once the power supply allows. Gamma (`setGamma(true)`) is off on purpose at low brightness.
- `SOUND_VOLUME` is 20 of 30. The MP3s are mastered loud; adjust on site.

## Environment Variables
- `MODULE` - Arduino project directory (default: halloween-bang)
- `DEV_PLATFORM` - Arduino platform (default: esp32:esp32)
- `DEV_BOARD` - Board identifier (default: esp32:esp32:esp32s3)
- `DEV_PORT` - Serial port (default: /dev/ttyACM0)
- `DEV_BOARD_BAUDRATE` - Monitor baudrate (default: 460800)
- `DEBUG` - Set to 1 to add -DDEBUG flag
- `VERBOSE` - Set to any value to add -DVERBOSE flag
