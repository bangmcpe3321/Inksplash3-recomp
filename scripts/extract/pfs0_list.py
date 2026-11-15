#!/usr/bin/env python3
"""List the files of a PFS0 (NSP) archive without extracting everything.

usage: pfs0_list.py <nsp> [--extract <name-substring> <outdir>]
"""
import os
import struct
import sys


def main():
    p = sys.argv[1]
    fsz = os.path.getsize(p)
    with open(p, "rb") as f:
        hdr = f.read(16)
        cnt, strsz = struct.unpack("<II", hdr[4:12])
        raw = f.read(0x18 * cnt)
        strtab = f.read(strsz)
        base = 16 + 0x18 * cnt + strsz
        entries = []
        for i in range(cnt):
            off, size, no, _ = struct.unpack("<QQII", raw[i * 0x18:(i + 1) * 0x18])
            name = strtab[no:strtab.index(b"\0", no)].decode()
            entries.append((name, off, size))

        print(f"{os.path.basename(p)}: {cnt} files, {fsz / 2**30:.2f} GiB, data base {base:#x}")
        total = 0
        for name, off, size in entries:
            print(f"  {name:48s} {size:12d}  ({size / 2**20:.1f} MiB)")
            total += size
        print(f"  total {total} bytes")

        if len(sys.argv) > 4 and sys.argv[2] == "--extract":
            sub, outdir = sys.argv[3], sys.argv[4]
            os.makedirs(outdir, exist_ok=True)
            for name, off, size in entries:
                if sub in name:
                    f.seek(base + off)
                    with open(os.path.join(outdir, name), "wb") as o:
                        o.write(f.read(size))
                    print(f"  extracted {name} ({size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
