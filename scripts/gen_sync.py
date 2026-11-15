#!/usr/bin/env python3
"""Copy a freshly generated C++ tree over the existing one, touching only files whose bytes differ.

gen.py rewrites every TU on every run; ninja rebuilds by mtime, so a one-instruction generator fix used to
recompile all 600+ TUs. Usage: gen_sync.py <new_dir> <live_dir>
"""
import os, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
changed = same = 0
for name in sorted(os.listdir(src)):
    a, b = os.path.join(src, name), os.path.join(dst, name)
    data = open(a, "rb").read()
    if os.path.exists(b) and open(b, "rb").read() == data:
        same += 1
        continue
    shutil.copyfile(a, b)   # new mtime -> ninja rebuilds exactly this TU
    changed += 1
print(f"gen_sync: {changed} changed, {same} unchanged")
