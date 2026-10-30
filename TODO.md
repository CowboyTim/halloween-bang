# TODO

Status as of 2026-10-16 (evening). "Won't do" items are kept on purpose, so the decision and the reason stay visible.

## Open

- **KILLSWITCH range (ESP-NOW only now).** The 433 MHz STX882 is not fitted and its transmit path was removed from the KILLSWITCH build (2026-10-30). ESP-NOW alone: 10-68% at 7 m, so range is still open. Still to do:
  - test the KILLSWITCH held in a hand / pocket with the body between it and the BUTTON,
  - if range is still a problem, fit the STX882 and SRX882 again (the BUTTON takes either link) and put the 433 MHz transmit path back in the KILLSWITCH build.
- **Wired killswitch on the BUTTON (GPIO 1):** code and schematic done, filter tested off-board, short-cable bench test OK (2026-10-16), not yet tried with the real 10 m cable. Still to test with the real cable: no false locks near mains cables or the strobe supply, release feels instant, shorting the wire expires after 15 s.
- **STROBO box** (add-on): one flash at the KABOOM now (150 ms, PWM brightness `STROBO_BRIGHTNESS`), no strobing (2026-10-27). Connected and flashed. Still to do: choose the brightness on site, check a full show with the box at its real place (`docs/strobo_schematic.svg`).
- **Boxes:** decided 2026-10-16: one EXPLOSION box and one STROBO box. The code still supports any number (one `[boards.*]` entry each).
- **Tests still to do:**
  - BANGER show without a DFPlayer (now the default build; the strip should run the full ~14 s, boot without the DFPlayer wait),
  - BANGER with the EXPLOSION box off (the show must not be delayed),
  - line-out: does its level follow the DFPlayer volume setting?
  - 1 kohm resistors on the DFPlayer TX/BUSY lines: measure the high levels at 5 V and the pin voltage (below about 3.6 V),
  - timing offset between the BANGER's and the box's sound (phone microphone).
- **BANGER's DFPlayer** (only with `-DBANGER_DFPLAYER`, not in the default build): if it is used, listen for noise during the white flash; if there is any, give it its own 5 V feed.
- **KILLSWITCH power:** USB-C cable + power bank (decided 2026-10-20). Still to test: that the power bank does not switch itself off at the unit's low current (about 90-100 mA), over a few hours.

## Done (2026-10-16)

- All boards flashed with production builds (no `--log`), which also brings the watchdog handler fix to the BUTTON and KILLSWITCH.
- Cold power-up of the EXPLOSION box and the BANGER tested (the DFPlayer ACK fix).
- SVG schematics checked (rendered, and looked at in a browser).

## Won't do

- **One build with the role stored in flash** (decided 2026-10-16). One build per role from `bang.py` / `boards.toml` is enough; the extra partition and the flash step are not worth it.
- **Splitting the BANGER into a separate LED board and sound board** (decided 2026-10-16). The BANGER stays lights + sound. Extra sound goes into EXPLOSION boxes, extra strobes into STROBO boxes.
- **Battery (LiPo, AA/AAA pack) for the KILLSWITCH** (decided 2026-10-20). USB-C + power bank is simpler: no charger, protection circuit, diode or power switch needed.
- **ESP-01 (ESP8266) as KILLSWITCH:** too awkward to program, needs its own firmware port.
- **ARM / guarded / COUNTDOWN design** and the countdown display that belonged to it: replaced by hold-to-lock.

## Ideas (undecided)

- S3 SuperMini as a second KILLSWITCH, for comparison (add a `[boards.*]` entry with chip `esp32s3`).
- A small amplifier with its own volume knob on the DFPlayer line-out.
