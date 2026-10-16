# 433 MHz link range test

Bench test for a raw ASK pair (STX882 / SRX882, or the MX-FS-03V / MX-05V). Not part of the show firmware.
Question: does 433 MHz reach the BUTTON better than ESP-NOW (10-68% at 7 m for the KILLSWITCH)?

The transmitter (ESP32-C3) sends a numbered packet every 150 ms, the KILLSWITCH's LOCK rate. The receiver
(ESP32-S3) prints every 4 s how many arrived and how many failed the CRC.

## Wiring

| | Transmitter (C3) | Receiver (S3) |
|---|---|---|
| module | STX882 | SRX882 |
| DATA | GPIO 4 | GPIO 6, through 1 kohm |
| VCC | 3.3 V first (see below) | 5 V |
| GND | GND | GND |
| ANT | straight wire, about 17 cm | straight wire, about 17 cm |

- The receiver's DATA output swings at its supply (5 V), and the S3 pins are not 5 V tolerant: keep the 1 kohm in series (the same rule as the DFPlayer lines).
- The product pages do not give the STX882's supply range or the SRX882's. The page lists "50 mW at 3.6 V" for the transmitter, so start at 3.3 V. Only try 5 V on the transmitter once you have seen its supply range on the module or in its datasheet.
- GPIO 7 is RadioHead's PTT pin: leave it unconnected. RadioHead configures GPIO 4, 6 and 7 on both boards, so do not use boards that have something wired to those pins (the BANGER: 4 = strip data, 7 = BUSY).
- Keep the antenna wires straight, away from the ESP and from metal.

## Build and flash

Compile with the role as a flag, then upload to the port of the board (`python3 bang.py list` shows the ports):

```
arduino-cli compile -b esp32:esp32:esp32c3 --build-property build.extra_flags="-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DESP32 -DRF_TX" tools/rf433_test
arduino-cli upload  -b esp32:esp32:esp32c3 -p /dev/ttyACMx tools/rf433_test

arduino-cli compile -b esp32:esp32:esp32s3 --build-property build.extra_flags="-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DESP32 -DRF_RX" tools/rf433_test
arduino-cli upload  -b esp32:esp32:esp32s3 -p /dev/ttyACMy tools/rf433_test
```

Afterwards put the show firmware back: `python3 bang.py deploy <board>`.

## Running it

1. Put the receiver on USB (monitor open) and the transmitter on a power bank.
2. Start close (1 m): expect nearly all 26 packets per window.
3. Move the transmitter to 4 m, 7 m and 8-10 m. Wait for at least two windows at each distance.
4. Compare with ESP-NOW: 10-68% at 7 m.

`[RX] 24 received of about 26 sent` is good. `nothing valid, bad CRC n` means noise or too weak a signal.
Note that RadioHead's ESP32 timer code was only compile-tested on this core (3.3.12): if nothing arrives even at 1 m, check `[RF] init FAILED` first.
