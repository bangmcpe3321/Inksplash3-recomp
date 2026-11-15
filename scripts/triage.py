#!/usr/bin/env python3
"""Boot-log and binary triage for the Splatoon 3 port.

  triage.py sym <main|sdk> <offset> [offset...]   nearest dynamic symbol for a guest offset
  triage.py crash <log>                           every [crash]/trap line mapped to symbols
  triage.py imports                               main imports vs runtime HLE vs sdk exports

Offsets are hex or decimal, relative to the module base (main 0x7100000000,
sdk 0x7180000000). Reads Work/data (override with SPLAT3_WORK).
"""
import json, os, re, struct, sys

WORK = os.environ.get("SPLAT3_WORK", os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/../..")
DATA = os.path.join(WORK, "data")
RT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "baseline", "totk-recomp")

def load_maps(mod):
    """offset -> name from the module's dynsym (defined symbols only)."""
    d = open(os.path.join(DATA, "img", mod + ".bin"), "rb").read()
    j = json.load(open(os.path.join(DATA, "img", mod + ".json")))
    dyn = j["dynamic"]; st, sr = dyn["SYMTAB"][0], dyn["STRTAB"][0]
    n = (dyn["STRTAB"][0] - st) // 24 if "HASH" not in dyn else struct.unpack_from("<I", d, dyn["HASH"][0] + 4)[0]
    out = []
    for i in range(n):
        s = st + i * 24
        o, info, shn = struct.unpack_from("<IBH", d, s)
        val = struct.unpack_from("<Q", d, s + 8)[0]
        if shn and 0 < o < len(d) - sr:
            e = d.index(b"\0", sr + o)
            out.append((val, d[sr + o:e].decode("utf-8", "replace")))
    out.sort()
    return out

def nearest(m, off):
    lo, hi = 0, len(m)
    while lo < hi:
        mid = (lo + hi) // 2
        if m[mid][0] <= off: lo = mid + 1
        else: hi = mid
    return m[lo - 1] if lo else (0, "?")

def fmt(mod, off):
    v, name = nearest(load_maps(mod), off)
    return f"{mod}+0x{off:x} ({name}+0x{off - v:x})"

def sym(args):
    mod = args[0]
    for a in args[1:]:
        print(fmt(mod, int(a, 0)))

def imports(_):
    """main imports vs runtime HLE names vs sdk exports."""
    d = open(os.path.join(DATA, "img", "main.bin"), "rb").read()
    j = json.load(open(os.path.join(DATA, "img", "main.json")))["dynamic"]
    st, sr = j["SYMTAB"][0], j["STRTAB"][0]; imps = set()
    for k, sk in (("JMPREL", "PLTRELSZ"), ("RELA", "RELASZ")):
        base, sz = j[k][0], j[sk][0]
        for i in range(0, sz, 24):
            _, ri, _ = struct.unpack_from("<QQq", d, base + i); sym = ri >> 32
            if sym and struct.unpack_from("<H", d, st + sym * 24 + 6)[0] == 0:
                o = struct.unpack_from("<I", d, st + sym * 24)[0]
                e = d.index(b"\0", sr + o); imps.add(d[sr + o:e].decode("utf-8", "replace"))
    hle = set()
    for root, _, files in os.walk(RT):
        for f in files:
            if not f.endswith((".cpp", ".h")): continue
            t = open(os.path.join(root, f), "rb").read().decode("utf-8", "replace")
            hle.update(re.findall(r'HLE_AS\(\s*\w+\s*,\s*"([^"]+)"', t))
            hle.update(re.findall(r"HLE\((\w+)\)", t))
    sdk = {n for _, n in load_maps("sdk")}
    miss = imps - hle - sdk
    print(f"main imports {len(imps)}  HLE {len(imps & hle)}  sdk-exported {len(imps & sdk)}  neither {len(miss)}")
    print("neither HLE nor sdk export:")
    for n in sorted(miss)[:40]: print("  ", n)

def crash(args):
    mainb, sdkb = 0x7100000000, 0x7180000000
    for line in open(args[0], errors="replace"):
        if "[crash]" in line or "unhandled:" in line or "unknown target" in line:
            print(line.rstrip())
            for m, tag, base in (("main", "f", mainb), ("sdk", "s", sdkb)):
                for off in re.findall(rf"\b{tag}_([0-9a-f]{{3,}})\b", line):
                    print("   ", fmt(m, int(off, 16)))

if __name__ == "__main__":
    cmd, *rest = sys.argv[1:]
    {"sym": sym, "imports": imports, "crash": crash}[cmd](rest)
