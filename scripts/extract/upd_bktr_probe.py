#!/usr/bin/env python3
"""Probe the BKTR trees of the Splatoon 3 update program NCA.

Derives the content key (AES-ECB-dec(titlekek[keygen-1], ticket titlekey block)),
then scans a window around the expected tree offsets for bucket-tree node headers
{ u32 0 ; u32 num_buckets ; u64 total_size } under a few candidate upper IVs.

No key material is printed.

usage: upd_bktr_probe.py <keys> <ticket.tik> <nca> <reloc_off> <enc_off>
"""
import struct
import subprocess
import sys

HELPER = "/w/obj/crypt_helper"


def key_lookup(path, name):
    for line in open(path, "r", errors="replace"):
        if line.startswith(name + " ") and "=" in line:
            return line.split("=", 1)[1].strip()
    return None


def ecbdec(key, data):
    return subprocess.run([HELPER, "ecbdec", key, data], capture_output=True,
                          text=True, check=True).stdout.strip()


def ctrfile(key, nca, foff, noff, length, upper, out="/w/obj/probe.bin"):
    subprocess.run([HELPER, "ctrfile", key, nca, hex(foff), hex(noff), hex(length), out, upper],
                   capture_output=True, check=True)
    return open(out, "rb").read()


def main():
    keys, tik, nca = sys.argv[1], sys.argv[2], sys.argv[3]
    rel_off, enc_off = int(sys.argv[4], 0), int(sys.argv[5], 0)

    enc_titlekey = open(tik, "rb").read()[0x180:0x190].hex()
    titlekek = key_lookup(keys, "titlekek_0d")
    ckey = ecbdec(titlekek, enc_titlekey)
    print("content key derived")

    uppers = ["0000000200000029", "0000000200000000", "0000000000000029",
              "0000000100000029", "2900000002000000", "0000002900000002"]
    for label, exp in (("reloc", rel_off), ("enc", enc_off)):
        print(f"== {label}: expected 0x{exp:x}")
        for up in uppers:
            for foff in range(exp - 0x1000, exp + 0x1000 + 1, 16):
                if foff < 0:
                    continue
                d = ctrfile(ckey, nca, foff, foff // 16, 16, up)
                pad, nb = struct.unpack("<II", d[:8])
                if pad == 0 and 1 <= nb <= 16:
                    tot, = struct.unpack("<Q", d[8:16])
                    print(f"   PLAUSIBLE upper={up} off=0x{foff:x} buckets={nb} total=0x{tot:x}")
        # also dump the expected location raw for eyeballing first words
        d = ctrfile(ckey, nca, exp, exp // 16, 16, uppers[0])
        print("   words@exp(upper=%s): %s" % (uppers[0], " ".join(f"{w:08x}" for w in struct.unpack("<4I", d))))


if __name__ == "__main__":
    main()
