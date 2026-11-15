#!/usr/bin/env python3
"""Filter a prod.keys file down to lines hactool 1.4.0 can parse.

Keeps only well-formed "name = hex" lines whose value length is a sane key/blob
size (32, 64, 512, 1056 hex chars). Some prod.keys files carry 34-char values
(extra byte) which make hactool abort with "must be 32 hex digits".
"""
import re
import sys

OK_LENS = {32, 64, 512, 1056}
pat = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*([0-9A-Fa-f]+)\s*$")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    kept = dropped = 0
    with open(src, "r", errors="replace") as f, open(dst, "w") as o:
        for ln in f:
            m = pat.match(ln)
            if not m or len(m.group(2)) not in OK_LENS:
                dropped += 1
                continue
            o.write(f"{m.group(1)} = {m.group(2)}\n")
            kept += 1
    print(f"keys: kept {kept}, dropped {dropped}")


if __name__ == "__main__":
    main()
