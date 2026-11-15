#!/usr/bin/env python3
"""Minimal PFS0 (NSP) extractor.

Layout (verified against the Splatoon 3 base NSP):
  0x00  char[4]  magic "PFS0"
  0x04  u32      file_count
  0x08  u32      string_table_size
  0x0C  u32      reserved
  0x10  entries[file_count]: { u64 offset, u64 size, u32 name_offset, u32 pad }  (stride 0x18)
  then  string table (NUL-separated names)
  then  data (offsets are relative to the byte right after the string table)
"""
import os
import sys
import struct

CHUNK = 16 * 1024 * 1024
ENTRY = 0x18


def main():
    if len(sys.argv) != 3:
        print("usage: pfs0_extract.py <in.nsp> <outdir>", file=sys.stderr)
        return 2
    src, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    with open(src, "rb") as f:
        hdr = f.read(16)
        if hdr[:4] != b"PFS0":
            print("not a PFS0 archive", file=sys.stderr)
            return 1
        count, strtab_size = struct.unpack("<II", hdr[4:12])
        print(f"PFS0: {count} files, string table {strtab_size} bytes")

        raw = f.read(ENTRY * count)
        entries = []
        for i in range(count):
            off, size, name_off, _pad = struct.unpack(
                "<QQII", raw[i * ENTRY:(i + 1) * ENTRY])
            entries.append((off, size, name_off))

        strtab = f.read(strtab_size)
        base = 16 + ENTRY * count + strtab_size
        print(f"data base: 0x{base:x}")

        total = 0
        for off, size, name_off in entries:
            name = strtab[name_off:strtab.index(b"\0", name_off)].decode()
            dst = os.path.join(outdir, name)
            print(f"  {name}  size={size} (0x{size:x})")
            f.seek(base + off)
            left = size
            with open(dst, "wb") as o:
                while left:
                    buf = f.read(min(CHUNK, left))
                    if not buf:
                        raise RuntimeError(f"unexpected EOF in {name}")
                    o.write(buf)
                    left -= len(buf)
            total += size
        print(f"done: {count} files, {total} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
