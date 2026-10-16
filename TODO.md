# TODO

Status as of 2026-10-16 (evening). "Won't do" items are kept on purpose, so the decision and the reason stay visible.

## Open

- **KILLSWITCH range: solved with a second link (433 MHz), still to finish.** ESP-NOW alone: 10-68% at 7 m. 433 MHz ASK (STX882/SRX882, 2000 bit/s, 17 cm wire antennas): 100% at 4 m, 7 m (one wall) and 10 m (two walls). The firmware now sends LOCK/FREE on both links and the BUTTON accepts either. Still to do:
  - test each link alone at distance (the BUTTON log says which delivered a lock),
  - test the KILLSWITCH held in a hand / pocket with the body between it and the BUTTON,
  - verify RadioHead's timer ISR and ESP-NOW run together on the BUTTON for a long time (hours), and that the lamp PWM is unaffected,
  - solder the SRX882 into the BUTTON (5 V, DATA via 1 kohm to GPIO 6, 17 cm antenna) and the STX882 into the KILLSWITCH; mount the antennas straight and away from metal,
  - check the STX882's supply range (3.3 V works; 5 V would give more power, but only after the datasheet allows it).
- **STROBO box** (add-on): works on the bench (tested 2026-10-16). Still to do: choose the LEDs and their drive values, solder it, rebuild the BANGER with the final box MACs, and check a full show with the box at its real place (`docs/strobo_schematic.svg`).
- **Boxes:** decided 2026-10-16: one EXPLOSION box and one STROBO box. The code still supports any number (one `[boards.*]` entry each).
- **Tests still to do:**
  - BANGER show without a DFPlayer / SD card (the strip should run the full ~14 s),
  - BANGER with the EXPLOSION box off (the show must not be delayed),
  - line-out: does its level follow the DFPlayer volume setting?
  - 1 kohm resistors on the DFPlayer TX/BUSY lines: measure the high levels at 5 V and the pin voltage (below about 3.6 V),
  - timing offset between the BANGER's and the box's sound (phone microphone).
- **BANGER's DFPlayer** shares the strip's 5 V: listen for noise during the white flash; if there is any, give it its own 5 V feed.
- **KILLSWITCH power:** USB power bank only. Battery use untested; most C3 SuperMinis have no charger.

## Done (2026-10-16)

- All boards flashed with production builds (no `--log`), which also brings the watchdog handler fix to the BUTTON and KILLSWITCH.
- Cold power-up of the EXPLOSION box and the BANGER tested (the DFPlayer ACK fix).
- SVG schematics checked (rendered, and looked at in a browser).

## Won't do

- **One build with the role stored in flash** (decided 2026-10-16). One build per role from `bang.py` / `boards.toml` is enough; the extra partition and the flash step are not worth it.
- **Splitting the BANGER into a separate LED board and sound board** (decided 2026-10-16). The BANGER stays lights + sound. Extra sound goes into EXPLOSION boxes, extra strobes into STROBO boxes.
- **ESP-01 (ESP8266) as KILLSWITCH:** too awkward to program, needs its own firmware port.
- **ARM / guarded / COUNTDOWN design** and the countdown display that belonged to it: replaced by hold-to-lock.

## Ideas (undecided)

- S3 SuperMini as a second KILLSWITCH, for comparison (add a `[boards.*]` entry with chip `esp32s3`).
- A small amplifier with its own volume knob on the DFPlayer line-out.
