# Bench test plan

Automated checks of the show chain over USB serial. Runner: `tools/bench/run_tests.py`. Each run saves
the serial logs and a `summary.txt` in `tools/bench/out/<time>/`. Exit status 1 if any check fails.

## Setup

- **Test builds only, with logging:** `python3 bang.py deploy <boards> --log --flags=-DTEST_SERIAL`.
  The test build reads one command per line on the USB serial: `status`, `fire`, `lock`, `free`.
- **Boards:** BANGER, one or more EXPLOSION boxes, a STROBO box (optional), one BUTTON (the spare or the
  real one, whichever is plugged in) and the KILLSWITCH (needed for the lock tests only).
- **Power:** every board on its normal supply. A power bank that cuts out at low current shows up as a
  missing board, not as a test failure, so check `python3 bang.py list` first.
- **Do not touch the KILLSWITCH's button** during the lock tests: it sends LOCK on its own.
- **Production again afterwards:** flash the production builds (no `--log`, no `-DTEST_SERIAL`).

## Tests

| Test | What it sends | What it checks |
|------|---------------|----------------|
| `status` (always first) | `status` to every board | every board answers `TEST status role=<ROLE>`: the test build is running |
| `single_show` | `fire` to the BUTTON | the BANGER starts and finishes the show; each EXPLOSION box gets `SND1` and `SND2`; the STROBO fires its flash; the show is 12-17 s (a full show is about 14.1 s); the box events come within 0.5 s of the BANGER sending them (host arrival times); the KABOOM animation ran frames; no reset |
| `repeat --shows N` | `fire` to the BANGER, N times, 2 s apart | every show is complete (the boxes and the STROBO, the length); no reset or crash in any board. This is the "fails after a few tries" test. The default N is 10 |
| `lock` | `lock` to the KILLSWITCH, then `fire` to the BUTTON, then `free` | the BUTTON locks while the KILLSWITCH is held and ignores a press; it unlocks on `free` and fires again |
| `lock_during_show` | `fire` to the BUTTON, `lock` 2 s later, `free` after the show | the show runs to its end while locked (a lock does not stop a show); the BUTTON goes to LOCKED when the show is over (still held); unlocks on `free` |
| `lock_expiry` | `lock` to the KILLSWITCH and hold it | the KILLSWITCH and the BUTTON both expire the lock after `LOCK_MAX_MS` (15 s), 13-18 s from the lock |

The timings checked in a run come from the logs (`SHOW done ... duration=`, `SND1 sent`, `STRB sent`,
`frames=`). The host arrival times of two boards' lines differ by the USB latency: a 0.5 s window is
far above that and far below a visible delay.

## What the tests do not cover

- **The LED strip's light.** The test sees the animation's frame count, not the light: check the strip
  with your own eyes (and a phone camera for the flash).
- **A missing box.** The show must run without an EXPLOSION or STROBO box. Unplug one and run
  `single_show` by hand (the checks for that box will fail, the show must still run).
- **The sound.** Only that the boxes received the message. Use a phone microphone for the timing offset.
- **Mains and radio interference** for the KILLSWITCH's 433 MHz link: see `TODO.md`.

## Results

Record each run here or in `TODO.md` with the date, the boards used and the `summary.txt` path.
Known results:

- 2026-10-27, `single_show` + `repeat --shows 15` (BANGER, EXPLOSION x2, STROBO, BUTTON spare): all
  checks pass, show length 14134-14141 ms. Before the fix to the DFPlayer search, one EXPLOSION box (no
  DFPlayer answering) hung in its search and was rebooted by the watchdog after 60 s (run
  `tools/bench/out/20261027-092840`). The lock tests have not been run yet (no KILLSWITCH connected).
