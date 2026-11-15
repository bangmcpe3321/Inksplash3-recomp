#!/usr/bin/env python3
"""Extract a Nintendo RomFS image from an NCA romfs section image.

usage: romfs_extract.py <section-image.bin> <romfs-offset> <outdir> [--list]

The section image is the de-sparsed, decrypted romfs section (IVFC levels +
romfs image). <romfs-offset> is where the romfs image starts inside it
(IVFC level 5 logical offset).
"""
import os
import struct
import sys

CHUNK = 8 * 1024 * 1024
EMPTY = 0xFFFFFFFF


def main():
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    img, base = sys.argv[1], int(sys.argv[2], 0)
    outdir = sys.argv[3]
    listonly = "--list" in sys.argv

    f = open(img, "rb")

    def rd(off, n):
        f.seek(off)
        d = f.read(n)
        if len(d) != n:
            raise RuntimeError(f"short read @0x{off:x} want {n}")
        return d

    (hsize, dh_off, dh_size, dm_off, dm_size,
     fh_off, fh_size, fm_off, fm_size, data_off) = struct.unpack("<10Q", rd(base, 0x50))
    print(f"romfs: header=0x{hsize:x} dir_meta={dm_off:#x}/{dm_size:#x} "
          f"file_meta={fm_off:#x}/{fm_size:#x} data={data_off:#x}", flush=True)

    dmeta = rd(base + dm_off, dm_size)
    fmeta = rd(base + fm_off, fm_size)

    def parse_dir(off):
        parent, sibling, child, fileoff, _hsh, nsz = struct.unpack("<IIIIII", dmeta[off:off + 0x18])
        name = dmeta[off + 0x18:off + 0x18 + nsz].decode("utf-8", "replace")
        return parent, sibling, child, fileoff, name

    def parse_file(off):
        parent, sibling, foff, fsz, _hsh, nsz = struct.unpack("<IIQQII", fmeta[off:off + 0x20])
        name = fmeta[off + 0x20:off + 0x20 + nsz].decode("utf-8", "replace")
        return parent, sibling, foff, fsz, name

    files = 0
    total = 0
    dirs = 0
    stack = [(0, outdir)]
    while stack:
        doff, path = stack.pop()
        _parent, _sib, child, fileoff, _name = parse_dir(doff)
        if not listonly:
            os.makedirs(path, exist_ok=True)
        dirs += 1

        c = child
        while c != EMPTY:
            _p, sib, _c, _fo, cname = parse_dir(c)
            stack.append((c, os.path.join(path, cname)))
            c = sib

        fe = fileoff
        while fe != EMPTY:
            _p, sib, foff, fsz, fname = parse_file(fe)
            dst = os.path.join(path, fname)
            files += 1
            total += fsz
            if listonly:
                print(f"{dst} 0x{fsz:x}")
            elif fsz:
                f.seek(base + data_off + foff)
                left = fsz
                with open(dst, "wb") as o:
                    while left:
                        buf = f.read(min(CHUNK, left))
                        if not buf:
                            raise RuntimeError("short read while extracting " + dst)
                        o.write(buf)
                        left -= len(buf)
            else:
                open(dst, "wb").close()
            fe = sib

    print(f"done: {dirs} dirs, {files} files, {total} bytes ({total / 2**30:.2f} GiB)", flush=True)
    f.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
