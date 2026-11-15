#!/usr/bin/env python3
"""Build a LibHac/hactool compatible title.keys file from NSP tickets.

The content key of these NSPs is
    AES-128-ECB-decrypt(titlekek[key_generation - 1], ticket.TitleKeyBlock[0x00:0x10])
where key_generation is the ticket's crypto type (byte 0x285) and the title key block
lives at ticket offset 0x180.  This was validated against hactool's IVFC verification
(DLC NCA) and against the decrypted sparse/BKTR tables and the update ExeFS PFS0.

usage: make_title_keys.py <prod.keys> <out title.keys> <ticket.tik> [<ticket.tik> ...]
No key material is printed.
"""
import subprocess
import sys

HELPER = "/w/obj/crypt_helper"


def key_lookup(path, name):
    for line in open(path, "r", errors="replace"):
        if line.startswith(name + " ") and "=" in line:
            return line.split("=", 1)[1].strip()
    raise SystemExit(f"missing key {name}")


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    keys, out, tickets = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = []
    for tik in tickets:
        d = open(tik, "rb").read()
        rights_id = d[0x2A0:0x2B0].hex()
        crypto = d[0x285]
        enc_titlekey = d[0x180:0x190].hex()
        kek = key_lookup(keys, "titlekek_%02x" % (crypto - 1))
        ck = subprocess.run([HELPER, "ecbdec", kek, enc_titlekey],
                            capture_output=True, text=True, check=True).stdout.strip()
        lines.append(f"{rights_id} = {ck}\n")
    with open(out, "w") as f:
        f.writelines(lines)
    print(f"wrote {out} ({len(lines)} title keys)")


if __name__ == "__main__":
    main()
