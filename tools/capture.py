#!/usr/bin/env python
r"""capture.py - take real screenshots of every game / app on the AmoledArcade over USB serial.

Run (pyserial for the port, pillow for PNGs; nothing is installed globally):
    uv run --with pyserial --with pillow tools\capture.py --port COM6 --out docs/screenshots
    uv run --with pyserial --with pillow tools\capture.py --port COM6 --one now.png
    uv run --with pyserial --with pillow tools\capture.py --port COM6 --only snake,tetris

Firmware protocol (line based, see firmware hw.cpp "remote control"): every command gets "OK <cmd>" or "ERR <reason>".
  SHOT -> "SHOT 368 448 RGB565LE <n>\\n" + n raw bytes + "\\nEND\\n" + "OK SHOT"
  LIST, OPEN G|A <i>, START, HOME, SETTINGS, APPS, SEL <row>, KEY L|R|B, KEY LH|RH <ms>, TAP x y, TILT x y | TILT OFF

Orientation: the frame buffer is the physical portrait panel, 368 wide x 448 tall, RGB565 little-endian (uint16 per
pixel, row-major). The firmware draws landscape pixel (x, y) at fb index x*368 + 367 - y, i.e. portrait column
c = 367 - y, row r = x. So landscape L(x, y) = P(367 - y, x), which is a 90 degree COUNTER-CLOCKWISE rotation of the
portrait picture (PIL: Image.ROTATE_90). Result is 448 x 368 with upright text (buttons on top).
"""
import argparse
import re
import sys
import time

import serial
from PIL import Image

PW, PH = 368, 448


class Remote:
    def __init__(self, port, baud=115200, timeout=3.0):
        self.ser = serial.Serial()          # created closed so DTR/RTS can be set before open (no board reset)
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = timeout
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def _readline(self, deadline):
        buf = bytearray()
        while time.time() < deadline:
            c = self.ser.read(1)
            if not c:
                continue
            if c == b"\n":
                return buf.decode("ascii", "replace").strip()
            buf += c
        raise TimeoutError("no line within timeout (got %r)" % bytes(buf[-40:]))

    def cmd(self, line, timeout=4.0):
        """Send a command, return (ok, text of the OK/ERR line). Unrelated log lines are skipped."""
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()
        deadline = time.time() + timeout
        while True:
            s = self._readline(deadline)
            if s.startswith("OK ") or s.startswith("ERR "):
                return s.startswith("OK "), s

    def lines(self, line, timeout=4.0):
        """Like cmd() but also returns the lines printed before OK (for LIST)."""
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()
        deadline = time.time() + timeout
        out = []
        while True:
            s = self._readline(deadline)
            if s.startswith("OK ") or s.startswith("ERR "):
                return s.startswith("OK "), out
            out.append(s)

    def shot(self):
        """Returns a landscape PIL image (448x368)."""
        self.ser.reset_input_buffer()
        self.ser.write(b"SHOT\n")
        self.ser.flush()
        deadline = time.time() + 6.0
        while True:
            s = self._readline(deadline)
            if s.startswith("SHOT "):
                break
            if s.startswith("ERR "):
                raise RuntimeError(s)
        parts = s.split()
        w, h, fmt, n = int(parts[1]), int(parts[2]), parts[3], int(parts[4])
        if (w, h, fmt, n) != (PW, PH, "RGB565LE", PW * PH * 2):
            raise RuntimeError("unexpected header: " + s)
        data = bytearray()
        deadline = time.time() + 15.0
        while len(data) < n and time.time() < deadline:
            chunk = self.ser.read(min(4096, n - len(data)))
            data += chunk
        if len(data) != n:
            raise TimeoutError("short frame %d/%d" % (len(data), n))
        tail = self.ser.read(5)
        if tail != b"\nEND\n":
            raise RuntimeError("bad trailer %r" % tail)
        return rgb565_to_landscape(bytes(data))

    def close(self):
        self.ser.close()


def rgb565_to_landscape(data):
    px = bytearray(PW * PH * 3)
    o = 0
    for i in range(0, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r = (v >> 11) & 31
        g = (v >> 5) & 63
        b = v & 31
        px[o] = (r << 3) | (r >> 2)
        px[o + 1] = (g << 2) | (g >> 4)
        px[o + 2] = (b << 3) | (b >> 2)
        o += 3
    portrait = Image.frombytes("RGB", (PW, PH), bytes(px))
    return portrait.transpose(Image.ROTATE_90)       # L(x,y) = P(367-y, x)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "x"


def save(img, outdir, base):
    img.save(outdir / (base + ".png"))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(outdir / (base + "_2x.png"))


def grab(rem, outdir, base, results, wait=0.0):
    """Shot with one retry. Records the result."""
    time.sleep(wait)
    for attempt in (1, 2):
        try:
            save(rem.shot(), outdir, base)
            results.append((base, True, ""))
            print("  saved", base)
            return True
        except Exception as e:  # noqa: BLE001
            err = str(e)
            time.sleep(0.5)
    results.append((base, False, err))
    print("  FAILED", base, err)
    return False


def do(rem, line, wait=0.0):
    ok, s = rem.cmd(line)
    if wait:
        time.sleep(wait)
    return ok, s


def capture_entry(rem, outdir, kind, idx, name, results, args):
    base = slug(name)
    print("%s %d %s" % (kind, idx, name))
    try:
        do(rem, "HOME", 0.3)
        ok, s = do(rem, "OPEN %s %d" % (kind, idx), args.settle)
        if not ok:
            results.append((base, False, s))
            print("  skip:", s)
            return
        menu_img = None
        try:
            menu_img = rem.shot()
        except Exception:  # noqa: BLE001
            pass
        ok, s = do(rem, "START", 0)
        if ok:                                   # had an options page
            if menu_img is not None:
                save(menu_img, outdir, base + "_menu")
                results.append((base + "_menu", True, ""))
                print("  saved", base + "_menu")
            time.sleep(args.play_wait)
        else:                                    # app without options: already running
            time.sleep(max(0.5, args.play_wait - args.settle))
        if args.nudge:
            do(rem, "KEY R", 0.25)
            do(rem, "TILT 0.30 0.12", 0.6)
        grab(rem, outdir, base, results)
        do(rem, "TILT OFF", 0)
    except Exception as e:  # noqa: BLE001
        results.append((base, False, str(e)))
        print("  error:", e)
        try:
            rem.cmd("TILT OFF")
        except Exception:  # noqa: BLE001
            pass
    finally:
        try:
            rem.cmd("HOME")
        except Exception:  # noqa: BLE001
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port, e.g. COM6")
    ap.add_argument("--out", default="docs/screenshots", help="output folder")
    ap.add_argument("--one", metavar="NAME.png", help="just save the current screen to this file and exit")
    ap.add_argument("--only", help="comma separated entry names (slug or index), e.g. snake,tetris")
    ap.add_argument("--settle", type=float, default=0.7, help="seconds to wait after OPEN")
    ap.add_argument("--play-wait", type=float, default=3.5, help="seconds after START (countdown)")
    ap.add_argument("--no-nudge", dest="nudge", action="store_false", help="no KEY/TILT nudge before the game shot")
    args = ap.parse_args()

    from pathlib import Path
    rem = Remote(args.port)
    try:
        if args.one:
            p = Path(args.one)
            p.parent.mkdir(parents=True, exist_ok=True)
            rem.shot().save(p)
            print("saved", p)
            return 0

        outdir = Path(args.out)
        outdir.mkdir(parents=True, exist_ok=True)
        ok, entries = rem.lines("LIST")
        if not ok or not entries:
            print("LIST failed - is the REMOTE firmware flashed and the right port selected?")
            return 2
        parsed = []
        for e in entries:
            m = re.match(r"([GA]) (\d+) (.+)", e)
            if m:
                parsed.append((m.group(1), int(m.group(2)), m.group(3)))
        print("%d entries" % len(parsed))
        results = []

        if not args.only:
            print("launcher")
            do(rem, "HOME", 0.5)
            grab(rem, outdir, "launcher", results)
            nrows = len([p for p in parsed if p[0] == "G"]) + 2
            for r in (nrows // 2, nrows - 1):
                do(rem, "SEL %d" % r, 0.4)
                grab(rem, outdir, "launcher_scrolled_%d" % r, results)
            do(rem, "APPS", 0.5)
            grab(rem, outdir, "apps_list", results)
            do(rem, "SETTINGS", 0.6)
            grab(rem, outdir, "settings", results)
            do(rem, "HOME", 0.3)

        want = [w.strip().lower() for w in args.only.split(",")] if args.only else None
        for kind, idx, name in parsed:
            if want and slug(name) not in want and "%s%d" % (kind.lower(), idx) not in want:
                continue
            capture_entry(rem, outdir, kind, idx, name, results, args)
        do(rem, "HOME", 0)

        bad = [r for r in results if not r[1]]
        print("\nsummary: %d saved, %d failed" % (len(results) - len(bad), len(bad)))
        for b, _, err in bad:
            print("  FAILED %s: %s" % (b, err))
        return 1 if bad else 0
    finally:
        rem.close()


if __name__ == "__main__":
    sys.exit(main())
