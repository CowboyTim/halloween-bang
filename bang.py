#!/usr/bin/env python3
"""Build, flash and monitor the Halloween Bang boards.

Boards are listed in boards.toml (MAC, chip, role). The serial port of a
board is found through its MAC: native-USB ESP32-S3/C3 boards report the MAC
as their USB serial number, so swapped /dev/ttyACM* ports do not matter and
nothing is reset just to look. esptool is used to identify new boards and to
flash; it refuses to write when the chip type does not match.

  python3 bang.py list                      ports, MACs and the board on each
  python3 bang.py identify /dev/ttyACM2     chip + MAC of a new board (boards.toml entry)
  python3 bang.py build   banger|button|killswitch|all
  python3 bang.py flash   banger button     flash the last build
  python3 bang.py deploy  all --log         build + flash, with serial logging (-DLOG_VERBOSE)
  python3 bang.py monitor banger button     serial output, prefixed with the board name

Needs Python 3.11+ (tomllib), pyserial, arduino-cli with the esp32:esp32 core.
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "halloween-bang")
BOARDS_FILE = os.path.join(HERE, "boards.toml")
DIST = os.path.join(HERE, "dist")
BUILD_CACHE = os.path.join(HERE, ".build")
PLATFORM = "esp32:esp32"
ROLES = ("BANGER", "BUTTON", "KILLSWITCH")

# Per chip: arduino-cli FQBN and esptool --chip. Both chips use the bootloader
# at 0x0, the partition table at 0x8000 and the app at 0x10000 (partitions.csv).
CHIPS = {
    "esp32s3": {"fqbn": "esp32:esp32:esp32s3", "esptool": "esp32s3"},
    "esp32c3": {"fqbn": "esp32:esp32:esp32c3", "esptool": "esp32c3"},
}
FLASH_LAYOUT = (
    ("0x0", "halloween-bang.ino.bootloader.bin"),
    ("0x8000", "halloween-bang.ino.partitions.bin"),
    ("0x10000", "halloween-bang.ino.bin"),
)
FLASH_BAUD = 921600

# The same flags build.sh always used; both chips run Serial over their USB-Serial/JTAG port.
BASE_FLAGS = "-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -D_ARDUINO_BLE_H_ -DESP32"
SECTION_FLAGS = "-ffunction-sections -fdata-sections -fno-exceptions"


def die(msg):
    sys.exit(f"bang: {msg}")


def norm_mac(mac):
    """'E4:B3:23:F9:43:F0', 'e4-b3-...' or 'e4b323f943f0' -> 'e4:b3:23:f9:43:f0' (None if not a MAC)."""
    h = re.sub(r"[^0-9a-fA-F]", "", mac or "")
    if len(h) != 12:
        return None
    return ":".join(h[i:i + 2] for i in range(0, 12, 2)).lower()


# ---- boards.toml ------------------------------------------------------------

def load_boards():
    with open(BOARDS_FILE, "rb") as f:
        cfg = tomllib.load(f)
    boards = {}
    for name, b in cfg.get("boards", {}).items():
        role = str(b.get("role", "")).upper()
        chip = str(b.get("chip", "")).lower()
        if role not in ROLES:
            die(f"boards.toml: [boards.{name}] role must be one of {', '.join(ROLES)}")
        if chip not in CHIPS:
            die(f"boards.toml: [boards.{name}] chip must be one of {', '.join(CHIPS)}")
        mac = b.get("mac", "")
        if mac and not norm_mac(mac):
            die(f"boards.toml: [boards.{name}] mac '{mac}' is not a MAC address")
        boards[name] = {"name": name, "role": role, "chip": chip,
                        "mac": norm_mac(mac), "banger": b.get("banger")}
    macs = [b["mac"] for b in boards.values() if b["mac"]]
    if len(macs) != len(set(macs)):
        die("boards.toml: the same MAC is listed twice")
    return boards


def select(boards, names):
    """Board names, roles (any case) or 'all' -> list of boards, in boards.toml order."""
    if not names:
        die("name a board, a role or 'all' (see 'bang.py list')")
    picked = []
    for n in names:
        if n.lower() == "all":
            hits = list(boards.values())
        elif n in boards:
            hits = [boards[n]]
        else:
            hits = [b for b in boards.values() if b["role"] == n.upper()]
        if not hits:
            die(f"no board or role '{n}' in boards.toml (have: {', '.join(boards)})")
        picked += [b for b in hits if b not in picked]
    return picked


def connected_only(picked, names, port):
    """With 'all', boards that have no mac yet are skipped for flash/monitor instead of failing."""
    if port or "all" not in [n.lower() for n in names]:
        return picked
    for b in picked:
        if not b["mac"]:
            print(f"(skipping {b['name']}: no mac in boards.toml)")
    return [b for b in picked if b["mac"]]


def banger_mac(boards, board):
    """MAC the BUTTON/KILLSWITCH firmware talks to: the board's 'banger' key, else the only BANGER."""
    if board["banger"]:
        peer = boards.get(board["banger"])
        if not peer or peer["role"] != "BANGER":
            die(f"[boards.{board['name']}] banger = '{board['banger']}' is not a BANGER board")
        bangers = [peer]
    else:
        bangers = [b for b in boards.values() if b["role"] == "BANGER"]
    if len(bangers) != 1:
        if board["role"] == "BANGER":
            return None
        die(f"{board['name']}: {len(bangers)} BANGER boards in boards.toml, "
            f"set banger = \"<name>\" in [boards.{board['name']}]")
    if not bangers[0]["mac"]:
        if board["role"] == "BANGER":
            return None
        die(f"{board['name']}: the BANGER board '{bangers[0]['name']}' has no mac in boards.toml")
    return bangers[0]["mac"]


# ---- ports ------------------------------------------------------------------

def list_ports():
    """[(device, mac or None, description)] for all USB serial ports."""
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        die("pyserial is missing: pip install pyserial (or apt install python3-serial)")
    out = []
    for p in sorted(lp.comports(), key=lambda p: p.device):
        if p.vid is None:
            continue
        out.append((p.device, norm_mac(p.serial_number), p.description or ""))
    return out


def find_port(board, override=None):
    if override:
        return override
    if not board["mac"]:
        die(f"{board['name']}: no mac in boards.toml yet; run 'bang.py identify <port>' "
            f"and fill it in, or pass --port")
    hits = [dev for dev, mac, _ in list_ports() if mac == board["mac"]]
    if not hits:
        die(f"{board['name']} ({board['mac']}) is not connected (see 'bang.py list')")
    return hits[0]


# ---- tools ------------------------------------------------------------------

def arduino_data_dir():
    """Same rule as the old build.sh: $ARDUINO_DIRECTORIES_DATA, else ./.arduino15 if it
    exists, else arduino-cli's own default (~/.arduino15)."""
    d = os.environ.get("ARDUINO_DIRECTORIES_DATA")
    if not d and os.path.isdir(os.path.join(HERE, ".arduino15")):
        d = os.path.join(HERE, ".arduino15")
    if d:
        os.environ["ARDUINO_DIRECTORIES_DATA"] = d
        return d
    try:
        r = subprocess.run(["arduino-cli", "config", "get", "directories.data"],
                           capture_output=True, text=True, check=True)
        return r.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return os.path.expanduser("~/.arduino15")


def version_key(path):
    return [int(x) if x.isdigit() else 0 for x in re.split(r"[.\-]", path)]


def esptool_cmd():
    """The esptool of the installed esp32 core, else one on PATH, else 'python3 -m esptool'."""
    found = glob.glob(os.path.join(arduino_data_dir(), "packages/esp32/tools/esptool_py/*/esptool"))
    if found:
        return [max(found, key=lambda p: version_key(os.path.basename(os.path.dirname(p))))]
    for name in ("esptool", "esptool.py"):
        if shutil.which(name):
            return [name]
    return [sys.executable, "-m", "esptool"]


def esptool_v5(cmd):
    """esptool 5 spells commands write-flash/chip-id, older ones write_flash/chip_id."""
    try:
        r = subprocess.run(cmd + ["version"], capture_output=True, text=True, timeout=30)
        m = re.search(r"(\d+)\.\d+", r.stdout + r.stderr)
        return bool(m) and int(m.group(1)) >= 5
    except (OSError, subprocess.TimeoutExpired):
        return True


V4_NAMES = {"write-flash": "write_flash", "chip-id": "chip_id",
            "default-reset": "default_reset", "hard-reset": "hard_reset"}


def run_esptool(args):
    """Run esptool, echo its output, return (exit code, output)."""
    cmd = esptool_cmd()
    if not esptool_v5(cmd):
        args = [V4_NAMES.get(a, a) for a in args]
    print("+ " + " ".join(cmd + args), flush=True)
    try:
        p = subprocess.Popen(cmd + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError as e:
        die(f"cannot run esptool: {e}")
    out = []
    for line in p.stdout:
        sys.stdout.write(line)
        out.append(line)
    return p.wait(), "".join(out)


def parse_esptool(out):
    """Chip ('esp32s3') and MAC from esptool output."""
    chip = re.search(r"Detecting chip type\.\.\. *(ESP32[-\w]*)", out) or \
        re.search(r"Chip (?:is|type:) *(ESP32[-\w]*)", out)
    mac = re.search(r"MAC: *([0-9a-fA-F:]{17})", out)
    return (chip.group(1).replace("-", "").lower() if chip else None,
            norm_mac(mac.group(1)) if mac else None)


# ---- commands ---------------------------------------------------------------

def out_dir(board):
    return os.path.join(DIST, board["chip"], board["role"].lower())


def cmd_list(args, boards):
    by_mac = {b["mac"]: b for b in boards.values() if b["mac"]}
    seen = set()
    print(f"{'PORT':14} {'MAC':18} {'BOARD':12} {'ROLE':11} CHIP")
    for dev, mac, desc in list_ports():
        b = by_mac.get(mac)
        if b:
            seen.add(b["name"])
            print(f"{dev:14} {mac:18} {b['name']:12} {b['role']:11} {b['chip']}")
        else:
            what = "unknown: bang.py identify " + dev if mac else "(no MAC: " + desc + ")"
            print(f"{dev:14} {mac or '-':18} {what}")
    for b in boards.values():
        if b["name"] not in seen:
            state = "not connected" if b["mac"] else "no mac in boards.toml"
            print(f"{'-':14} {b['mac'] or '-':18} {b['name']:12} {b['role']:11} {b['chip']}  ({state})")


def cmd_identify(args, boards):
    rc, out = run_esptool(["--port", args.port, "chip-id"])
    chip, mac = parse_esptool(out)
    if rc or not mac:
        die(f"esptool could not read {args.port}")
    known = [b for b in boards.values() if b["mac"] == mac]
    print()
    if known:
        b = known[0]
        print(f"{args.port}: {mac} is [boards.{b['name']}] ({b['role']}, {b['chip']})")
        if chip and chip != b["chip"]:
            print(f"WARNING: boards.toml says chip = \"{b['chip']}\", esptool found {chip}")
    else:
        print("New board, add to boards.toml (pick a name and role):\n")
        print(f"[boards.NAME]\nmac  = \"{mac}\"\nchip = \"{chip or '?'}\"\nrole = \"BUTTON\"")


def build_flags(board, boards, args):
    flags = [BASE_FLAGS, "-D" + board["role"]]
    peer = banger_mac(boards, board)
    if peer:
        flags.append("-DBANGER_MAC=" + ",".join("0x" + x for x in peer.split(":")))
    if args.log:
        flags.append("-DLOG_VERBOSE")
    if os.environ.get("DEBUG") == "1":
        flags.append("-DDEBUG")
    extra = (os.environ.get("EXTRA_FLAGS", "") + " " + " ".join(args.flags or [])).split()
    for f in extra:
        if f[2:] in ROLES and f.startswith("-D") and f[2:] != board["role"]:
            die(f"{board['name']} is a {board['role']}, but the extra flags say {f}")
    flags += [f for f in extra if f != "-D" + board["role"]]
    return " ".join(flags)


def build(board, boards, args):
    chip = CHIPS[board["chip"]]
    flags = build_flags(board, boards, args)
    dst = out_dir(board)
    os.makedirs(dst, exist_ok=True)
    print(f"== build {board['role']} for {board['chip']} -> {os.path.relpath(dst, HERE)}")
    print(f"   flags: {flags}", flush=True)
    arduino_data_dir()
    cmd = ["arduino-cli", "compile", "-b", chip["fqbn"],
           "--output-dir", dst,
           "--build-path", os.path.join(BUILD_CACHE, f"{board['chip']}-{board['role'].lower()}"),
           "--build-property", f"compiler.cpp.extra_flags={flags} {SECTION_FLAGS}",
           "--build-property", f"compiler.c.extra_flags={flags} {SECTION_FLAGS}",
           "--build-property", "compiler.c.elf.extra_flags=-O2 -Wl,--gc-sections",
           "--build-property", f"compiler.S.extra_flags={flags} {SECTION_FLAGS}",
           "--build-property", "compiler.ar.extra_flags=",
           "--build-property", "compiler.ldflags.extra_flags=-Wl,--gc-sections",
           "--build-property", "build.flags.lto=false",
           "--build-property", f"build.extra_flags={flags}",
           "--build-property", "build.custom_partitions=partitions",
           "--build-property", "upload.maximum_size=1851392",
           "--board-options", "PartitionScheme=no_ota",
           SKETCH]
    if args.verbose:
        cmd[2:2] = ["-v"]
    if subprocess.run(cmd).returncode:
        die(f"build of {board['role']} for {board['chip']} failed")
    with open(os.path.join(dst, "build.json"), "w") as f:
        json.dump({"role": board["role"], "chip": board["chip"], "flags": flags,
                   "built": time.strftime("%Y-%m-%d %H:%M:%S")}, f, indent=1)


def cmd_build(args, boards):
    done = set()
    for b in select(boards, args.boards):
        key = (b["chip"], b["role"])     # one build per chip + role
        if key not in done:
            build(b, boards, args)
            done.add(key)


def flash(board, args):
    src = out_dir(board)
    bins = [(addr, os.path.join(src, name)) for addr, name in FLASH_LAYOUT]
    missing = [p for _, p in bins if not os.path.exists(p)]
    if missing:
        die(f"{board['name']}: no build in {os.path.relpath(src, HERE)}, run 'bang.py deploy {board['name']}'")
    try:
        with open(os.path.join(src, "build.json")) as f:
            info = json.load(f)
    except (OSError, ValueError):
        info = {}
    newest_src = max(os.path.getmtime(p) for p in glob.glob(os.path.join(SKETCH, "*.*")))
    if os.path.getmtime(bins[-1][1]) < newest_src:
        print(f"WARNING: {board['name']}: the build is older than the sketch sources")
    port = find_port(board, args.port)
    print(f"== flash {board['name']} ({board['role']}, {board['chip']}) on {port}"
          f"  built {info.get('built', '?')}  flags: {info.get('flags', '?')}", flush=True)
    rc, out = run_esptool(
        ["--chip", CHIPS[board["chip"]]["esptool"], "--port", port, "--baud", str(FLASH_BAUD),
         "--before", "default-reset", "--after", "hard-reset",
         "write-flash", "-z", "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]
        + [x for addr, path in bins for x in (addr, path)])
    chip, mac = parse_esptool(out)
    if rc:
        die(f"flashing {board['name']} on {port} failed (esptool exit {rc}); "
            f"is a monitor still open on {port}?")
    if board["mac"] and mac and mac != board["mac"]:
        die(f"{port} turned out to be {mac}, not {board['name']} ({board['mac']})")
    print(f"== {board['name']} flashed OK ({chip or '?'}, {mac or '?'})")


def cmd_flash(args, boards):
    picked = connected_only(select(boards, args.boards), args.boards, args.port)
    if args.port and len(picked) > 1:
        die("--port works with one board only")
    for b in picked:
        flash(b, args)


def cmd_deploy(args, boards):
    picked = select(boards, args.boards)
    to_use = connected_only(picked, args.boards, args.port)
    if args.port and len(picked) > 1:
        die("--port works with one board only")
    if not args.port:
        for b in to_use:       # fail before building when a board is not there
            find_port(b)
    cmd_build(args, boards)
    for b in to_use:
        flash(b, args)
    if args.monitor:
        cmd_monitor(args, boards)


_say_lock = threading.Lock()


def say(line):
    """One whole line per write, so the lines of two boards never interleave."""
    with _say_lock:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()


def monitor_one(board, port_override, prefix, stop):
    import serial
    tag = f"[{board['name']}] " if prefix else ""
    while not stop.is_set():
        try:
            port = find_port(board, port_override)
        except SystemExit:
            time.sleep(0.5)          # gone (reset, replugged): wait for it to come back
            continue
        try:
            s = serial.Serial(port, 115200, timeout=0.2)
            # Opening raises DTR+RTS (no reset). Drop RTS first: on the USB-Serial/JTAG
            # port RTS set with DTR clear means reset, which pyserial's own order hits.
            s.rts = False
            s.dtr = False
        except (serial.SerialException, OSError):
            time.sleep(0.5)
            continue
        say(f"{tag}--- connected on {port} ---")
        buf = b""
        try:
            while not stop.is_set():
                buf += s.read(4096)
                *lines, buf = buf.split(b"\n")
                for line in lines:
                    say(tag + line.decode("utf-8", "replace").rstrip("\r"))
        except (serial.SerialException, OSError):
            say(f"{tag}--- disconnected ---")
        finally:
            s.close()


def cmd_monitor(args, boards):
    picked = connected_only(select(boards, args.boards), args.boards, args.port)
    if args.port and len(picked) > 1:
        die("--port works with one board only")
    for b in picked:
        if not b["mac"] and not args.port:
            die(f"{b['name']}: no mac in boards.toml, pass --port")
    stop = threading.Event()
    threads = [threading.Thread(target=monitor_one, args=(b, args.port, len(picked) > 1, stop),
                                daemon=True) for b in picked]
    for t in threads:
        t.start()
    print("--- monitor: Ctrl-C to quit ---", flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        stop.set()


def cmd_update(args, boards):
    urls = os.environ.get("DEV_URLS", "https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json")
    arduino_data_dir()
    for c in (["core", "install", PLATFORM],
              ["--additional-urls", urls, "update"],
              ["--additional-urls", urls, "core", "install", PLATFORM],
              ["--additional-urls", urls, "lib", "update-index"],
              ["--additional-urls", urls, "lib", "install", "DFRobotDFPlayerMini"],
              ["--additional-urls", urls, "lib", "upgrade"],
              ["--additional-urls", urls, "board", "list"]):
        if subprocess.run(["arduino-cli"] + c).returncode:
            die("arduino-cli " + " ".join(c) + " failed")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="serial ports, their MAC and board")
    p = sub.add_parser("identify", help="chip and MAC of the board on a port (resets it)")
    p.add_argument("port")
    sub.add_parser("update", help="install/update the esp32 core and libraries")
    for name, hlp in (("build", "compile"), ("flash", "flash the last build"),
                      ("deploy", "build + flash"), ("monitor", "serial output")):
        p = sub.add_parser(name, help=hlp)
        p.add_argument("boards", nargs="+", help="board name, role or 'all'")
        p.add_argument("--port", help="use this port instead of finding it by MAC (one board only)")
        if name in ("build", "deploy"):
            p.add_argument("--log", action="store_true", help="serial logging on (-DLOG_VERBOSE)")
            p.add_argument("--flags", action="append", help="extra compiler flags, written as --flags='-DX -UY' (also $EXTRA_FLAGS)")
            p.add_argument("-v", "--verbose", action="store_true", help="verbose arduino-cli compile")
        if name == "deploy":
            p.add_argument("--monitor", action="store_true", help="monitor the boards afterwards")
    args = ap.parse_args()
    boards = load_boards()
    {"list": cmd_list, "identify": cmd_identify, "update": cmd_update, "build": cmd_build,
     "flash": cmd_flash, "deploy": cmd_deploy, "monitor": cmd_monitor}[args.cmd](args, boards)


if __name__ == "__main__":
    main()
