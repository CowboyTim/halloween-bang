#!/usr/bin/env python3
"""Bench test of the Halloween Bang boards. Needs the TEST build on every board in the test:

  python3 bang.py deploy banger button killswitch explosion strobo --log --flags=-DTEST_SERIAL

(-DTEST_SERIAL reads one command per line on the USB serial: status | fire | lock | free; --log turns the
logging on, the test reads the log lines.) Never flash a TEST build to a board that is used for a show.

  python3 tools/bench/run_tests.py                       all tests
  python3 tools/bench/run_tests.py single_show           one test (see TESTS below)
  python3 tools/bench/run_tests.py repeat --shows 20     the repeat test with 20 shows

Boards are found by MAC from boards.toml. Each run's serial logs are saved in tools/bench/out/<time>/,
with a summary.txt. Exit status 1 if any check failed.
"""

import argparse
import datetime
import os
import re
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)
import bang  # noqa: E402  (boards.toml, port lookup by MAC)

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# lines that mean a board reset or crashed; any of them fails the test that saw it
RESET_RE = re.compile(r"rst:|Guru Meditation|Brownout|panic|abort\(\)|Task watchdog|WDT|Backtrace")

# the show as the BANGER logs it: a full show is about 14 s; outside 12-17 s something is wrong
SHOW_MIN_MS, SHOW_MAX_MS = 12000, 17000
HOST_SYNC_S = 0.5          # two boards' events that should be simultaneous: host arrival times within this


class Board:
    def __init__(self, cfg, port, out_dir):
        self.name, self.role, self.port = cfg["name"], cfg["role"], port
        self.lines = []                # (host time, text)
        self.lock = threading.Lock()
        self.log = open(os.path.join(out_dir, cfg["name"] + ".log"), "w", encoding="utf-8")
        self.ser = None

    def start(self, stop):
        import serial
        self.ser = serial.Serial(self.port, 115200, timeout=0.2)
        # opening raises DTR and RTS (no reset): clear RTS first, as bang.py monitor does
        self.ser.rts = False
        self.ser.dtr = False
        self.thread = threading.Thread(target=self._read, args=(stop,), daemon=True)
        self.thread.start()

    def _read(self, stop):
        buf = b""
        while not stop.is_set():
            try:
                buf += self.ser.read(4096)
            except Exception:
                return
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                text = raw.decode("utf-8", "replace").rstrip("\r")
                now = time.monotonic()
                with self.lock:
                    self.lines.append((now, text))
                self.log.write(f"{now:.3f} {text}\n")
                self.log.flush()

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

    def since(self, t):
        with self.lock:
            return [(ht, tx) for ht, tx in self.lines if ht >= t]

    def find(self, pattern, t):
        """First line at or after host time t that matches pattern: (host time, match) or None."""
        rx = re.compile(pattern)
        for ht, tx in self.since(t):
            m = rx.search(tx)
            if m:
                return ht, m
        return None

    def wait(self, pattern, t, timeout):
        """Wait until pattern shows up after t. Returns (host time, match) or None on timeout."""
        deadline = time.monotonic() + timeout
        while True:
            hit = self.find(pattern, t)
            if hit:
                return hit
            if time.monotonic() > deadline:
                return None
            time.sleep(0.05)


class Checks:
    def __init__(self):
        self.results = []             # (test, ok, text)
        self.test = ""

    def check(self, ok, text):
        self.results.append((self.test, bool(ok), text))
        print(f"  [{'PASS' if ok else 'FAIL'}] {text}", flush=True)
        return ok

    def begin(self, test):
        self.test = test
        print(f"\n== {test}", flush=True)


def role_board(boards, role):
    """The board of a role (the one BUTTON that is plugged in, the BANGER)."""
    for b in boards.values():
        if b.role == role:
            return b
    return None


def boards_of(boards, role):
    return [b for b in boards.values() if b.role == role]


LAST_FIRE = [0.0]      # host time of the last "fire" to the BUTTON: it waits WAIT_TIME_BUTTON (16 s) after a show


# ---- tests --------------------------------------------------------------------------------------

def test_status(boards, c):
    c.begin("status: every board runs the TEST build")
    t = time.monotonic()
    for b in boards.values():
        b.send("status")
    for b in boards.values():
        hit = b.wait(rf"TEST status role={b.role}", t, 3)
        c.check(hit, f"{b.name} ({b.role}) answers status" + ("" if hit else " - no answer: is it the TEST build?"))
    return True


def show_events(boards, t0, c, banger, expl, strobo):
    """Checks one show started at host time t0; returns the BANGER's show duration in ms (or None)."""
    dur = None
    hit = banger.wait(r"SHOW start", t0, 3)
    c.check(hit, "BANGER starts the show")
    hit = banger.wait(r"SHOW done ms=\d+ duration=(\d+)", t0, 25)
    if c.check(hit, "BANGER finishes the show"):
        dur = int(hit[1].group(1))
        c.check(SHOW_MIN_MS <= dur <= SHOW_MAX_MS, f"show length {dur} ms (expected {SHOW_MIN_MS}-{SHOW_MAX_MS})")
    for b in expl:
        for n in (1, 2):
            hit = b.wait(rf"SND{n} received", t0, 3)
            c.check(hit, f"{b.name} gets SND{n}")
    for b in strobo:
        hit = b.wait(r"STRB: flash on", t0, 3)
        c.check(hit, f"{b.name} fires its flash")
    return dur


def test_single_show(boards, c):
    c.begin("single_show: one show from the BUTTON, every box gets its message")
    button = role_board(boards, "BUTTON")
    banger = role_board(boards, "BANGER")
    expl, strobo = boards_of(boards, "EXPLOSION"), boards_of(boards, "STROBO")
    t0 = time.monotonic()
    button.send("fire")
    LAST_FIRE[0] = t0
    show_events(boards, t0, c, banger, expl, strobo)
    # the box events must come within HOST_SYNC_S of the BANGER sending them
    sent = banger.find(r"SND1 sent", t0)
    for b in expl:
        got = b.find(r"SND1 received", t0)
        if sent and got:
            d = got[0] - sent[0]
            c.check(abs(d) <= HOST_SYNC_S, f"{b.name} SND1 {d*1000:+.0f} ms against the BANGER's send")
    sent = banger.find(r"STRB sent", t0)
    for b in strobo:
        got = b.find(r"STRB: flash on", t0)
        if sent and got:
            d = got[0] - sent[0]
            c.check(abs(d) <= HOST_SYNC_S, f"{b.name} flash {d*1000:+.0f} ms against the BANGER's STRB")
    hit = banger.find(r"Animation LED 2: DONE frames=(\d+)", t0)
    c.check(hit and int(hit[1].group(1)) > 0, f"KABOOM animation ran ({hit[1].group(1) if hit else 'no DONE line'} frames)")
    reset = [tx for b in boards.values() for _, tx in b.since(t0) if RESET_RE.search(tx)]
    c.check(not reset, "no reset or crash during the show" + (f": {reset[0]}" if reset else ""))


def test_repeat(boards, c, shows):
    c.begin(f"repeat: {shows} shows back to back from the BANGER (the 'few tries' failure)")
    banger = role_board(boards, "BANGER")
    expl, strobo = boards_of(boards, "EXPLOSION"), boards_of(boards, "STROBO")
    durations = []
    for i in range(1, shows + 1):
        t0 = time.monotonic()
        banger.send("fire")
        ok_before = len(c.results)
        dur = show_events(boards, t0, c, banger, expl, strobo)
        # the per-show checks above print a lot: keep only the failures of this show
        failed = [r for r in c.results[ok_before:] if not r[1]]
        print(f"  show {i}: {'ok' if not failed else 'FAILED'}" + (f", {dur} ms" if dur else ""), flush=True)
        if dur:
            durations.append(dur)
        time.sleep(2)
    reset = [tx for b in boards.values() for _, tx in b.since(0) if RESET_RE.search(tx)]
    c.check(not reset, f"no reset or crash in {shows} shows" + (f": {reset[0]}" if reset else ""))
    if durations:
        print(f"  show length: min {min(durations)} ms, max {max(durations)} ms", flush=True)


def test_lock(boards, c):
    c.begin("lock: KILLSWITCH held locks the BUTTON, released unlocks it")
    button = role_board(boards, "BUTTON")
    ks = role_board(boards, "KILLSWITCH")
    # the BUTTON only takes a press while READY: wait out a show from an earlier test
    if LAST_FIRE[0]:
        button.wait(r"BUTTON state 3 -> 2", LAST_FIRE[0], 20)
    t = time.monotonic()
    ks.send("lock")
    c.check(ks.wait(r"KILLSWITCH held: sending LOCK", t, 3), "KILLSWITCH sends LOCK")
    c.check(button.wait(r"killswitch held", t, 3), "BUTTON is locked")
    t = time.monotonic()
    button.send("fire")
    c.check(not button.wait(r"ADHD style sending BANG", t, 2), "BUTTON ignores a press while locked")
    t = time.monotonic()
    ks.send("free")
    c.check(ks.wait(r"KILLSWITCH released: sending FREE", t, 3), "KILLSWITCH sends FREE")
    c.check(button.wait(r"killswitch released, button works again", t, 3), "BUTTON is unlocked")
    t = time.monotonic()
    button.send("fire")
    LAST_FIRE[0] = t
    c.check(button.wait(r"ADHD style sending BANG", t, 3), "BUTTON fires again after the release")
    banger = role_board(boards, "BANGER")
    banger.wait(r"SHOW done", t, 25)


def test_lock_during_show(boards, c):
    c.begin("lock_during_show: a lock during the show does not stop it; the BUTTON locks after it")
    button = role_board(boards, "BUTTON")
    ks = role_board(boards, "KILLSWITCH")
    banger = role_board(boards, "BANGER")
    t0 = time.monotonic()
    button.send("fire")
    LAST_FIRE[0] = t0
    c.check(button.wait(r"ADHD style sending BANG", t0, 3), "show requested from the BUTTON")
    time.sleep(2)
    ks.send("lock")
    hit = banger.wait(r"SHOW done ms=\d+ duration=(\d+)", t0, 25)
    c.check(hit and SHOW_MIN_MS <= int(hit[1].group(1)) <= SHOW_MAX_MS,
            "the show ran to the end while locked")
    hit = button.wait(r"BUTTON state 3 -> 0", t0, 20)
    c.check(hit, "BUTTON locks when the show is over (still held)")
    ks.send("free")
    c.check(button.wait(r"killswitch released, button works again", t0, 3), "BUTTON unlocks on release")


def test_lock_expiry(boards, c):
    c.begin("lock_expiry: a switch left on expires after LOCK_MAX_MS (15 s), on both sides")
    button = role_board(boards, "BUTTON")
    ks = role_board(boards, "KILLSWITCH")
    t = time.monotonic()
    ks.send("lock")
    hit = ks.wait(r"lock expired", t, 25)
    c.check(hit and 13 <= hit[0] - t <= 18, f"KILLSWITCH expires the lock at {(hit[0]-t) if hit else float('nan'):.1f} s")
    c.check(button.wait(r"lock expired", t, 25), "BUTTON expires the lock as well")
    ks.send("free")
    c.check(ks.wait(r"KILLSWITCH released", t, 3), "KILLSWITCH released")


TESTS = {
    "single_show": (test_single_show, ["BUTTON", "BANGER"]),
    "repeat": (test_repeat, ["BANGER"]),
    "lock": (test_lock, ["BUTTON", "KILLSWITCH"]),
    "lock_during_show": (test_lock_during_show, ["BUTTON", "BANGER", "KILLSWITCH"]),
    "lock_expiry": (test_lock_expiry, ["BUTTON", "KILLSWITCH"]),
}
ORDER = ["single_show", "repeat", "lock", "lock_during_show", "lock_expiry"]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("tests", nargs="*", help=f"tests to run: {', '.join(ORDER)} (default: all)")
    p.add_argument("--shows", type=int, default=10, help="shows in the repeat test (default 10)")
    args = p.parse_args()
    names = args.tests or ORDER
    for n in names:
        if n not in TESTS:
            p.error(f"unknown test {n}")

    cfg = bang.load_boards()
    present = {mac: dev for dev, mac, _ in bang.list_ports()}
    wanted = {"BANGER", "BUTTON", "KILLSWITCH", "EXPLOSION", "STROBO"}
    # every connected board of these roles, but only one BUTTON (a spare is not tested alongside it)
    boards_cfg = {}
    button_taken = False
    for b in cfg.values():
        if b["role"] not in wanted or b["mac"] not in present:
            continue
        if b["role"] == "BUTTON":
            if button_taken:
                continue
            button_taken = True
        boards_cfg[b["name"]] = b
    if not boards_cfg:
        sys.exit("no boards connected (see bang.py list)")
    need = set(r for n in names for r in TESTS[n][1])
    missing = sorted(r for r in need if not any(b["role"] == r for b in boards_cfg.values()))
    if missing:
        sys.exit(f"not connected: {', '.join(missing)}")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(OUT_DIR, stamp)
    os.makedirs(out_dir, exist_ok=True)
    stop = threading.Event()
    boards = {}
    for key, b in boards_cfg.items():
        boards[key] = Board(b, present[b["mac"]], out_dir)
        boards[key].start(stop)
    time.sleep(1.5)                  # let the boards print their boot lines

    c = Checks()
    print(f"boards: {', '.join(f'{b.name} ({b.role}) on {b.port}' for b in boards.values())}")
    print(f"logs: {out_dir}")
    test_status(boards, c)
    for n in names:
        fn, _ = TESTS[n]
        if n == "repeat":
            fn(boards, c, args.shows)
        else:
            fn(boards, c)
    stop.set()

    failed = [r for r in c.results if not r[1]]
    summary = [f"{t}: {'PASS' if ok else 'FAIL'} - {txt}" for t, ok, txt in c.results]
    with open(os.path.join(out_dir, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(summary) + "\n")
    print(f"\n{len(c.results) - len(failed)} of {len(c.results)} checks passed. Summary: {out_dir}/summary.txt")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
