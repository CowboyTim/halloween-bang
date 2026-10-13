# Halloween Bang - ESP32 Halloween Decoration

A ESP32-based Halloween decoration with sound and LED effects. Can be triggered wirelessly (ESP-NOW) or locally (button press).

## Two Operation Modes

### BANGER Mode (default)
- Receives "BANG" messages via ESP-NOW from other devices
- Plays sound effects and LED animations when triggered wirelessly
- Use: `EXTRA_FLAGS=-DBANGER DEV_PORT=/dev/ttyACM0 bash ./build.sh deploy`

### BUTTON Mode
- Press physical button to send "BANG" messages via ESP-NOW
- Can trigger other BANGER devices wirelessly
- Use: `EXTRA_FLAGS=-DBUTTON DEV_PORT=/dev/ttyACM1 bash ./build.sh deploy`

## Build Commands

```
# BANGER mode (wireless trigger)
EXTRA_FLAGS=-DBANGER DEV_PORT=/dev/ttyACM0 bash ./build.sh deploy

# BUTTON mode (local button)
EXTRA_FLAGS=-DBUTTON DEV_PORT=/dev/ttyACM1 bash ./build.sh deploy
```

## Audio Setup (MP3 for DFPlayer Mini)

The DFPlayer Mini requires specific audio format. Convert your audio files using:

```
python convert_for_dfplayer.py input_file.mp3 output_file.mp3
```

or with custom output name:

```
python convert_for_dfplayer.py input_file.mp3 -o 0001.mp3
```

This converts to 44.1kHz, 192kbps constant bitrate MP3 compatible with the DFPlayer Mini.

The firmware plays two sound effects:
- **Sound 1**: Whooshing effect (file 0001.mp3)
- **Sound 2**: Kaboom/explosion effect

Both should be placed in the project directory and converted using the script above.

## Development Notes

### Board GPIO Pinouts
- **BANGER mode**: Requires **ESP32 S3 Zero** - uses GPIO 4 (LED strip DIN), GPIO 12/13 (DFPlayer TX/RX), GPIO 7 (BUSY pin), GPIO 21 (builtin LED), GPIO 8 (button input pullup)
- **BUTTON mode**: Requires **ESP32 S3 SuperMini** - same GPIO assignments for button input and ESP-NOW communication

### Why these boards?
The firmware uses specific GPIO pins for the DFPlayer Mini UART (GPIO 12 TX, GPIO 13 RX) and LED strip (GPIO 4). The ESP32 S3 Zero and SuperMini have the correct pinout for these connections. Other ESP32 variants may have different pin mappings.

## Environment Variables
- `MODULE` - Arduino project directory (default: halloween-bang)
- `DEV_PLATFORM` - Arduino platform (default: esp32:esp32)
- `DEV_BOARD` - Board identifier (default: esp32:esp32:esp32s3)
- `DEV_PORT` - Serial port (default: /dev/ttyACM0)
- `DEV_BOARD_BAUDRATE` - Monitor baudrate (default: 460800)
- `DEBUG` - Set to 1 to add -DDEBUG flag
- `VERBOSE` - Set to any value to add -DVERBOSE flag