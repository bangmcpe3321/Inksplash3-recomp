#!/usr/bin/env python3
"""Print NSO0 header details + segment layout and check segment contiguity.

Header layout (switchbrew):
  0x00 magic "NSO0" | 0x04 version | 0x08 reserved | 0x0C flags (bit0 = compressed)
  0x10 .text   file_off | 0x14 mem_off | 0x18 size
  0x1C module name offset
  0x20 .rodata file_off | 0x24 mem_off | 0x28 size
  0x2C module object offset
  0x30 .data   file_off | 0x34 mem_off | 0x38 size
  0x3C bss size | 0x40 build id (0x20)
  0x60 compressed sizes (3 x u32, present when flags & 1)

usage: nso_info.py <file.nso> [...]
"""
import struct
import sys


def main():
    for p in sys.argv[1:]:
        with open(p, "rb") as f:
            d = f.read(0x100)
        magic = d[:4].decode("latin1")
        ver, = struct.unpack("<I", d[4:8])
        flags, = struct.unpack("<I", d[0x0C:0x10])
        segs = []
        for name, base in (("text", 0x10), ("rodata", 0x20), ("data", 0x30)):
            off, mem, sz = struct.unpack("<III", d[base:base + 0x0C])
            segs.append((name, off, mem, sz))
        bss, = struct.unpack("<I", d[0x3C:0x40])
        build_id = d[0x40:0x60]
        csz = struct.unpack("<III", d[0x60:0x6C])

        print(f"== {p}")
        print(f"   magic={magic} version={ver} flags={flags:#x} compressed={bool(flags & 1)}")
        print(f"   build_id={build_id.hex()}")
        prev = None
        ok = True
        for i, (name, off, mem, sz) in enumerate(segs):
            print(f"   {name:7s} file_off={off:#010x} mem_off={mem:#010x} size={sz:#x} ({sz})")
            if prev is not None and off != prev:
                print(f"      !! not contiguous: expected {prev:#x}")
                ok = False
            # when compressed, file offsets advance by the compressed sizes
            prev = off + (csz[i] if (flags & 1) else sz)
        print(f"   bss={bss:#x} file_end={prev:#x} mem_end={prev + bss:#x} contiguous={ok}")
        if flags & 1:
            print(f"   compressed: text={csz[0]:#x} rodata={csz[1]:#x} data={csz[2]:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
