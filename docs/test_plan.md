# Bench test plan

Automated checks of the show chain over USB serial. Runner: `tools/bench/run_tests.py`. Each run saves
the serial logs and a `summary.txt` in `tools/bench/out/<time>/`. Exit status 1 if any check fails.

## Setup

- **Test builds only, with logging:** `python3 bang.py deploy <boards> --log --flags=-DTEST_SERIAL`.
  The test build reads one command per line on the USB serial: `status`, `fire`, `lock`, `free`, and on
  the BUTTON `hold`, `chatter`, `release`, `glitch` (they stand in for the button pin).
- **Boards:** BANGER, one or more EXPLOSION boxes, a STROBO box (optional), one BUTTON (the spare or the
  real one, whichever is plugged in) and the KILLSWITCH (needed for the lock tests only).
- **Power:** every board on its normal supply. A power bank that cuts out at low current shows up as a
  missing board, not as a test failure, so check `python3 bang.py list` first.
- **Do not touch the KILLSWITCH's button** during the lock tests: it sends LOCK on its own.
- **Production again afterwards:** flash the production builds (no `--log`, no `-DTEST_SERIAL`).

## Run time

Every show is followed by the 60 s cooldown, so the full suite takes about half an hour. For a quick run, flash
test builds with a shorter cooldown: `--flags="-DTEST_SERIAL -DSHOW_COOLDOWN_MS=5000"`. The runner reads the
wait and the cooldown from the `status` replies and adapts. Never use a shortened cooldown for a show.

## Tests

| Test | What it sends | What it checks |
|------|---------------|----------------|
| `status` (always first) | `status` to every board | every board answers `TEST status role=<ROLE>`: the test build is running |
| `single_show` | `fire` to the BUTTON | the BANGER starts and finishes the show; each EXPLOSION box gets `SND1` and `SND2`; the STROBO fires its flash; the show is 12-17 s (a full show is about 14.1 s); the box events come within 0.5 s of the BANGER sending them (host arrival times); the KABOOM animation ran frames; no reset |
| `repeat --shows N` | `fire` to the BANGER, N times, each after the cooldown | every show is complete (the boxes and the STROBO, the length); no reset or crash in any board. This is the "fails after a few tries" test. The default N is 4 (each show waits out the 60 s cooldown) |
| `cooldown` | `fire` to the BANGER, again right after the show, again after the cooldown | the BANG in the cooldown is dropped; the cooldown lasts `SHOW_COOLDOWN_MS` (60 s); the BANG after it starts a show |
| `mash_button` | `fire` 30 times, 0.2 s apart | one BANG and one show for the whole burst |
| `button_glitch` | `glitch` 20 times (an interrupt edge with the pin HIGH again: noise) | no BANG, no show; the BUTTON logs the ignored glitches |
| `button_stuck` | `hold` for one show + cooldown + 20 s, then `release`, then `fire` | ONE show while stuck (not one after every cooldown); the BUTTON reports the stuck button (lamp flicker); letting go is not a press; a real press afterwards works |
| `button_chatter` | `chatter` (5 ms on / 5 ms off) for 20 s | no BANG, no show |
| `lock` | `lock` to the KILLSWITCH, then `fire` to the BUTTON, then `free` | the BUTTON locks while the KILLSWITCH is held and ignores a press; it unlocks on `free` and fires again |
| `lock_storm` | 20 quick `lock` / `free` cycles | no lock expires; the BUTTON fires afterwards (not stuck locked) |
| `lock_press_race` | `lock` and `fire` within 50 ms, 5 rounds | the BUTTON is unlocked after each release and fires afterwards |
| `lock_during_show` | `fire` to the BUTTON, `lock` 2 s later, `free` after the show | the show runs to its end while locked (a lock does not stop a show); the BUTTON goes to LOCKED when the show is over (still held); unlocks on `free` |
| `lock_expiry` | `lock` to the KILLSWITCH and hold it | the KILLSWITCH and the BUTTON both expire the lock after `LOCK_MAX_MS` (30 s), 28-33 s from the lock |

The timings checked in a run come from the logs (`SHOW done ... duration=`, `SND1 sent`, `STRB sent`,
`frames=`). The host arrival times of two boards' lines differ by the USB latency: a 0.5 s window is
far above that and far below a visible delay.

## What the tests do not cover

- **The LED strip's light.** The test sees the animation's frame count, not the light: check the strip
  with your own eyes (and a phone camera for the flash).
- **A missing box.** The show must run without an EXPLOSION or STROBO box. Unplug one and run
  `single_show` by hand (the checks for that box will fail, the show must still run).
- **The sound.** Only that the boxes received the message. Use a phone microphone for the timing offset.
- **Mains and radio interference** for the KILLSWITCH's ESP-NOW link: see `TODO.md`.

## Results

Record each run here or in `TODO.md` with the date, the boards used and the `summary.txt` path.
Known results:

- 2026-10-27, `single_show` + `repeat --shows 15` (BANGER, EXPLOSION x2, STROBO, BUTTON spare): all
  checks pass, show length 14134-14141 ms. Before the fix to the DFPlayer search, one EXPLOSION box (no
  DFPlayer answering) hung in its search and was rebooted by the watchdog after 60 s (run
  `tools/bench/out/20261027-092840`). 2026-10-27, lock tests (KILLSWITCH, BUTTON spare, BANGER, EXPLOSION x2, STROBO): `lock`, `lock_during_show`
  and `lock_expiry` all pass, 19 of 19 checks in the last run (`tools/bench/out/20261027-095358`). The
  KILLSWITCH expires its lock at 15.2 s; the BUTTON unlocks at the same moment, from the FREE the KILLSWITCH
  sends at its expiry (its own expiry message may not show up in the log).
