# TODO

Status as of 2026-11-02. "Won't do" items are kept on purpose, so the decision and the reason stay visible.

## Open

- **Lock as a LOCK stream (AGENTS.md, decided 2026-11-02), code not done yet.** The KILLSWITCH toggles locked mode on each press and sends LOCK every 150 ms while locked; the BUTTON unlocks 1 s after the last LOCK or on FREE. To do:
  - BUTTON: remove the 30 s `LOCK_MAX_MS` expiry from the radio lock (keep it for the wired killswitch), keep the 1 s stream timeout and the FREE unlock,
  - KILLSWITCH: toggle on press, stream LOCK while locked, FREE 3x on unlock, no expiry, unlocked at power-up,
  - bench tests: rewrite `lock_expiry`, the "BUTTON unlocks on release" check and `lock_during_show` for the toggle (release no longer unlocks), add a stream-stop test (KILLSWITCH reboot or unplug unlocks the BUTTON within 1 s),
  - README and `docs/test_plan.md`: remove the 30 s and hold-to-lock wording.
- **KILLSWITCH stopped working once (fixed by a power-cycle).** Likely cause: a stuck-low input (GPIO 3 or BOOT GPIO 9, either one counts as held), which the old 30 s expiry latched until a reset. Check the button and its cable for a short to GND, and whether the box blinks fast with nothing pressed. Under the new design a stuck-low pin only toggles on its edge, so re-test after the change.
- **KILLSWITCH range (ESP-NOW only).** The 433 MHz STX882 is not fitted and its transmit path is out of the KILLSWITCH build. ESP-NOW alone reached 10-68% at 7 m. Still to test: the KILLSWITCH held in a hand or pocket with the body between it and the BUTTON.
- **KILLSWITCH power:** the 2.2 Ah bank ran the KILLSWITCH about 4 h (0.32 Ah at the output) before it went dead. The cell is worn out (about 8 years old). Replace it with a new 18650 that has its own protection, and check the new run.
- **Wired killswitch on the BUTTON (GPIO 1):** tested off-board and on a short cable. Still to test with the real 10 m cable: no false locks near mains cables or the strobe supply, the release feels instant, and a shorted wire expires after 30 s.
- **STROBO box:** now a burst of 6 flashes (80 ms on, 120 ms apart, 2 s hard stop) at the KABOOM, flashed and committed. Still to do: pick the brightness on site (`STROBO_BRIGHTNESS`) and check a full show with the box at its real place (`docs/strobo_schematic.svg`).
- **Show with both strips:** the kaboom peak on the 2 A supply line was 0.68 A total (0.26 A above idle). Still to do: confirm the ring is not dim or yellow at the peak. Only if it is, add the feed at LED 300.
- **Bench tests still to do:**
  - BANGER with the EXPLOSION boxes off (the show must not be delayed),
  - line-out: does its level follow the DFPlayer volume setting? (only with `-DBANGER_DFPLAYER` / EXPLOSION boxes),
  - 1 kohm resistors on the DFPlayer TX/BUSY lines: measure the high levels at 5 V and the pin voltage (below about 3.6 V).
- **BANGER's DFPlayer** (only with `-DBANGER_DFPLAYER`, not in the default build): if it is used, listen for noise during the white flash; if there is any, give it its own 5 V feed.

## Done (recent)

- 2026-11-02: AGENTS.md lock design (LOCK stream, dead-man switch), committed and pushed.
- 2026-10-30: KILLSWITCH built without the 433 MHz transmit path; the BUTTON keeps its SRX882 receiver path. STROBO burst of flashes. Spare BUTTON flashed. All connected boards on production firmware.
- 2026-10-29: 600-LED strip (fuse 0-419, ring 420-599, `FUSE_SCALE`), 60 s show cooldown on both sides, BANGER blue LED during the cooldown, press filter and stuck lamp on the BUTTON. Power measured with the inline USB meter (`README.md`, "Power measurements").
- 2026-10-28: the bench suite passes (79/79 at its last run, before the 600-LED and STROBO changes).
- 2026-10-16: all boards on production builds; EXPLOSION cold power-up with the DFPlayer ACK fix; schematics checked.

## Won't do

- **One build with the role stored in flash** (decided 2026-10-16). One build per role from `bang.py` / `boards.toml` is enough; the extra partition and the flash step are not worth it.
- **Splitting the BANGER into a separate LED board and sound board** (decided 2026-10-16). The BANGER stays lights + sound. Extra sound goes into EXPLOSION boxes, extra strobes into STROBO boxes.
- **Battery (LiPo, AA/AAA pack) for the KILLSWITCH** (decided 2026-10-20). USB-C + power bank is simpler: no charger, protection circuit, diode or power switch needed.
- **ESP-01 (ESP8266) as KILLSWITCH:** too awkward to program, needs its own firmware port.
- **ARM / guarded / COUNTDOWN design** and the countdown display that belonged to it: replaced by the lock stream.
- **Latched lock in the BUTTON (lock until unlocked, no timer)** (rejected 2026-11-02): a lost FREE would leave the BUTTON locked and the show blocked until a power-cycle. The lock now lives in the KILLSWITCH's LOCK stream instead.
- **Hold-to-lock on the KILLSWITCH** (replaced 2026-11-02 by the press toggle): the lock must outlast the hold.

## Ideas (undecided)

- S3 SuperMini as a second KILLSWITCH, for comparison (add a `[boards.*]` entry with chip `esp32s3`).
- A small amplifier with its own volume knob on the DFPlayer line-out.
