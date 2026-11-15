#!/usr/bin/env python3
"""Host-side controller harness for the Splatoon 2 / TotK-runtime port.

The runtime (baseline/totk-recomp/runtime/hle_hid.cpp) merges a small state file into its HID input when
TOTK_PAD_FILE is set: 24 bytes, little endian --

    "PAD1" | u32 seq | u64 buttons | s16 lx | s16 ly | s16 rx | s16 ry

This script is the other end: it writes that file at a fixed rate while it runs a scenario, so the game
can be driven with no physical pad. If the script stops, the runtime releases the pad after 500 ms.

Scenario DSL (one step per line, '#' comments):

    wait <ms>                     sleep
    press <BTN> [<ms>]            tap a button (default 80 ms)
    hold <BTN>                    hold until released
    release <BTN>                 release
    stick <lx> <ly> [<rx> <ry>]   left (and optional right) stick, -32768..32767
    waitfor <text> [<ms>]         wait until the log file contains <text> (timeout optional, default 60 s)
    mark <text>                   print a marker
    quit                          stop

Buttons: A B X Y LS RS L R ZL ZR PLUS MINUS LEFT UP RIGHT DOWN (NpadButton bit order).

Examples:
    input_sequencer.py scenarios/boot-to-tutorial.txt --log work/logs/vk-10.log
    TOTK_PAD_FILE=/w/pad.state ...   (runtime side; the host path is work/pad.state)
    input_sequencer.py --interactive            # type keys, the pad follows (q to quit)

Interactive keys: a=A b=B x=X y=Y, arrows=d-pad, Enter=A, Esc=PLUS, m=MINUS, z=ZL, c=ZR, f=R, g=L,
w/s/a/d = left stick, i/k/j/l = right stick, q=quit.
"""
import argparse
import os
import struct
import sys
import threading
import time

BUTTONS = {n: i for i, n in enumerate(
    ["A", "B", "X", "Y", "LS", "RS", "L", "R", "ZL", "ZR", "PLUS", "MINUS", "LEFT", "UP", "RIGHT", "DOWN"])}

def pack(seq, buttons, lx, ly, rx, ry):
    return b"PAD1" + struct.pack("<IQhhhh", seq & 0xFFFFFFFF, buttons & 0xFFFFFFFFFFFFFFFF, lx, ly, rx, ry)

class Pad:
    def __init__(self, path, rate):
        self.path, self.rate = path, rate
        self.lock = threading.Lock()
        self.buttons = 0
        self.lx = self.ly = self.rx = self.ry = 0
        self.seq = 0
        self.stop = False
        self.t = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.t.start()
        return self

    def _run(self):
        period = 1.0 / self.rate
        while not self.stop:
            with self.lock:
                self.seq = (self.seq + 1) & 0xFFFFFFFF
                data = pack(self.seq, self.buttons, self.lx, self.ly, self.rx, self.ry)
            tmp = self.path + ".tmp"
            try:
                with open(tmp, "wb") as f:
                    f.write(data)
                os.replace(tmp, self.path)       # atomic: the runtime never reads a half-written packet
            except OSError as e:
                print(f"[seq] cannot write {self.path}: {e}", file=sys.stderr)
                return
            time.sleep(period)

    def set_button(self, name, down):
        bit = 1 << BUTTONS[name]
        with self.lock:
            self.buttons = (self.buttons | bit) if down else (self.buttons & ~bit)

    def set_sticks(self, lx, ly, rx, ry):
        with self.lock:
            self.lx, self.ly, self.rx, self.ry = lx, ly, rx, ry

    def release_all(self):
        with self.lock:
            self.buttons = 0
            self.lx = self.ly = self.rx = self.ry = 0

    def close(self):
        self.release_all()
        time.sleep(0.1)          # let one zeroed packet go out
        self.stop = True
        self.t.join(timeout=1.0)

def log_contains(path, needle):
    try:
        with open(path, "rb") as f:
            f.seek(max(0, os.path.getsize(path) - 8 * 1024 * 1024))
            return needle.encode() in f.read()
    except OSError:
        return False

def run_block(pad, steps, log, lo=0, hi=None):
    """Execute steps[lo:hi]. 'loop <n>' ... 'end' repeats its block n times (nestable)."""
    if hi is None:
        hi = len(steps)
    i = lo
    while i < hi:
        no, line = steps[i]
        i += 1
        if not line:
            continue
        w = line.split()
        op = w[0].lower()
        if op == "loop":
            n = int(w[1]) if len(w) > 1 else 1
            depth, j = 1, i
            while j < hi:
                o = steps[j][1].split()[0].lower() if steps[j][1] else ""
                if o == "loop": depth += 1
                elif o == "end":
                    depth -= 1
                    if depth == 0: break
                j += 1
            for _ in range(n):
                run_block(pad, steps, log, i, j)
            i = j + 1
            continue
        if op == "end":
            return
        try:
            if op == "wait":
                time.sleep(int(w[1]) / 1000.0)
            elif op == "press":
                btn = w[1].upper()
                ms = int(w[2]) if len(w) > 2 else 80
                pad.set_button(btn, True)
                time.sleep(ms / 1000.0)
                pad.set_button(btn, False)
            elif op == "hold":
                pad.set_button(w[1].upper(), True)
            elif op == "release":
                pad.set_button(w[1].upper(), False)
            elif op == "stick":
                v = [int(x) for x in w[1:5]] + [0] * (4 - len(w[1:5]))
                pad.set_sticks(*v[:4])
            elif op == "waitfor":
                # the needle may contain spaces (e.g. a log line): take the rest, minus an optional
                # trailing numeric timeout token
                rest = w[1:]
                timeout = 60000
                if len(rest) > 1 and rest[-1].isdigit():
                    timeout = int(rest[-1]); rest = rest[:-1]
                needle = " ".join(rest)
                t0 = time.time()
                while not log_contains(log, needle):
                    if (time.time() - t0) * 1000 > timeout:
                        print(f"[seq] line {no}: waitfor '{needle}' timed out after {timeout} ms", file=sys.stderr)
                        break
                    time.sleep(0.05)
            elif op == "mark":
                print(f"[seq] mark: {' '.join(w[1:])}")
            elif op == "quit":
                return
            else:
                print(f"[seq] line {no}: unknown step '{op}'", file=sys.stderr)
        except (IndexError, ValueError) as e:
            print(f"[seq] line {no}: bad step '{line}': {e}", file=sys.stderr)

def run_scenario(pad, path, log):
    with open(path) as f:
        steps = [(i + 1, ln.split("#", 1)[0].strip()) for i, ln in enumerate(f)]
    run_block(pad, steps, log)

KEYMAP = {"a": "A", "b": "B", "x": "X", "y": "Y", "z": "ZL", "c": "ZR", "f": "R", "g": "L",
          "m": "MINUS", "v": "LS", "n": "RS"}

def run_interactive(pad):
    import termios, tty, select
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        print("[seq] interactive: a/b/x/y, arrows=d-pad, Enter=A, Esc=PLUS, z/c/f/g, w/s/a/d left stick, i/k/j/l right stick, q=quit")
        held = set()
        while True:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                ch = os.read(fd, 1).decode("latin1")
                if ch in ("q", "\x03"):
                    break
                if ch == "\x1b":
                    # arrow keys arrive as ESC [ A/B/C/D; a bare ESC is PLUS
                    r2, _, _ = select.select([fd], [], [], 0.01)
                    if r2:
                        seq = os.read(fd, 2).decode("latin1")
                        m = {"[A": "UP", "[B": "DOWN", "[C": "RIGHT", "[D": "LEFT"}.get(seq)
                        if m:
                            pad.set_button(m, True); held.add(m)
                        continue
                    pad.set_button("PLUS", True); held.add("PLUS")
                    continue
                if ch == "\r":
                    pad.set_button("A", True); held.add("A")
                elif ch in KEYMAP:
                    pad.set_button(KEYMAP[ch], True); held.add(KEYMAP[ch])
                elif ch in "wsad":
                    lx, ly = pad.lx, pad.ly
                    if ch == "w": ly = 32767
                    if ch == "s": ly = -32767
                    if ch == "a": lx = -32767
                    if ch == "d": lx = 32767
                    pad.set_sticks(lx, ly, pad.rx, pad.ry)
                elif ch in "ikjl":
                    rx, ry = pad.rx, pad.ry
                    if ch == "i": ry = 32767
                    if ch == "k": ry = -32767
                    if ch == "j": rx = -32767
                    if ch == "l": rx = 32767
                    pad.set_sticks(pad.lx, pad.ly, rx, ry)
            else:
                for b in list(held):
                    pad.set_button(b, False)
                held.clear()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)

def main():
    ap = argparse.ArgumentParser(description="host-side controller harness")
    ap.add_argument("scenario", nargs="?", help="scenario file (DSL above)")
    ap.add_argument("--pad", default=os.environ.get("TOTK_PAD_FILE_HOST", "pad.state"),
                    help="state file to write (host path that the runtime sees as TOTK_PAD_FILE)")
    ap.add_argument("--log", default="", help="game log file for 'waitfor'")
    ap.add_argument("--rate", type=float, default=60.0, help="writes per second (default 60)")
    ap.add_argument("--interactive", action="store_true", help="drive from the keyboard")
    a = ap.parse_args()
    if not a.scenario and not a.interactive:
        ap.error("give a scenario file or --interactive")
    pad = Pad(a.pad, a.rate).start()
    try:
        if a.scenario:
            run_scenario(pad, a.scenario, a.log)
        if a.interactive or not a.scenario:
            run_interactive(pad)
    except KeyboardInterrupt:
        pass
    finally:
        pad.close()

main()
