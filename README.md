# Halloween Bang - Setup Guide

## Build Commands

### BANGER Mode (ESP-NOW wireless trigger)
```
EXTRA_FLAGS=-DBANGER DEV_PORT=/dev/ttyACM0 bash ./build.sh deploy
```
- Firmware receives "BANG" messages via ESP-NOW
- Plays sound effects and LED animations when triggered wirelessly

### BUTTON Mode (local button trigger)
```
EXTRA_FLAGS=-DBUTTON DEV_PORT=/dev/ttyACM1 bash ./build.sh deploy
```
- Firmware sends "BANG" messages via ESP-NOW when button pressed
- Can trigger other BANGER devices wirelessly

## Environment Variables
- `MODULE` - Arduino project directory (default: halloween-bang)
- `DEV_PLATFORM` - Arduino platform (default: esp32:esp32)
- `DEV_BOARD` - Board identifier (default: esp32:esp32:esp32s3)
- `DEV_PORT` - Serial port (default: /dev/ttyACM0)
- `DEV_BOARD_BAUDRATE` - Monitor baudrate (default: 460800)
- `DEBUG` - Set to 1 to add -DDEBUG flag
- `VERBOSE` - Set to any value to add -DVERBOSE flag

## Operation Modes
- **BANGER**: Receives "BANG" via ESP-NOW from other devices (including BUTTON mode devices)
- **BUTTON**: Press physical button to send "BANG" via ESP-NOW