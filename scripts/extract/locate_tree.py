#!/usr/bin/env python3
"""Locate/verify the BKTR bucket-tree node blocks of the update NCA section.

Scans a window of file offsets, CTR-decrypts 16 bytes with candidate upper IVs and
reports offsets where the decrypted node header looks like {u32 0, u32 buckets<=16}.
Never prints key material.

usage: locate_tree.py <content-key-hex> <nca> <expected-file-offset> <expected-buckets>
"""
import struct
import subprocess
import sys

HELPER = "/w/obj/crypt_helper"


def main():
    key, nca, exp, nb_want = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4])
    uppers = ["0000000200000029", "0000000200000000", "0000000000000029",
              "0000000100000029", "2900000002000000"]
    hits = []
    for up in uppers:
        for foff in range(exp - 0x2000, exp + 0x2000, 16):
            if foff < 0:
                continue
            out = "/w/obj/lt.bin"
            r = subprocess.run([HELPER, "ctrfile", key, nca, hex(foff), hex(foff // 16), "16", out, up],
                               capture_output=True)
            if r.returncode != 0:
                continue
            d = open(out, "rb").read()
            pad, nb = struct.unpack("<II", d[:8])
            if pad == 0 and nb == nb_want:
                tot, = struct.unpack("<Q", d[8:16])
                hits.append((up, foff, nb, tot))
    for up, foff, nb, tot in hits:
        print(f"HIT upper={up} file_off=0x{foff:x} buckets={nb} total=0x{tot:x}")
    if not hits:
        print("no hits")


if __name__ == "__main__":
    main()
