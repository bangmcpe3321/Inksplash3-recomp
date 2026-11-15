#!/usr/bin/env python3
"""Produce the merged v11.3.0 RomFS for Splatoon 3.

Steps (all inside the container, paths under /w):
  1. derive the content keys of the patch and base Program NCAs from their tickets
  2. decrypt the base SparseInfo bucket tree
  3. decrypt the patch BKTR relocation tree and encryption tree
  4. merge patch + base into one 10.08 GB virtual romfs image (merge_bktr)
  5. extract the RomFS tree with romfs_extract.py (romfs image at IVFC level5 = 0x12D8000)
  6. delete the intermediate image

usage: upd_romfs.py <prod.keys> <patch.tik> <base.tik> <patch.nca> <base.nca> [outdir]
No key material is printed.
"""
import os
import subprocess
import sys

HELPER = "/w/obj/crypt_helper"
MERGE = "/w/obj/merge_bktr"
OBJ = "/w/obj"

# fixed offsets for this title (see data/EXTRACT_REPORT.md / ncainfo output)
BASE_SPARSE_OFF = 0xC5CDC000        # base romfs SparseInfo bucket tree (absolute in NCA)
BASE_SPARSE_SIZE = 0x8000
BASE_SPARSE_UPPER = "0000000200010000"   # {generation 1<<16, secure_value 2}
BASE_SEC_START = 0x1C000

PATCH_RELOC_OFF = 0x1AD9E7600       # 0x4000 + patchInfo.relocationTreeOffset
PATCH_RELOC_SIZE = 0x24000
PATCH_ENC_OFF = 0x1ADA0B600         # 0x4000 + patchInfo.encryptionTreeOffset
PATCH_ENC_SIZE = 0x18000
PATCH_UPPER = "0000000200000029"    # {generation 0x29, secure_value 2}

ROMFS_OFFSET = 0x12D8000            # IVFC level5 logical offset of the merged image


def key_lookup(path, name):
    for line in open(path, "r", errors="replace"):
        if line.startswith(name + " ") and "=" in line:
            return line.split("=", 1)[1].strip()
    raise SystemExit(f"missing key {name}")


def ecbdec(key, data):
    return subprocess.run([HELPER, "ecbdec", key, data], capture_output=True,
                          text=True, check=True).stdout.strip()


def ctrfile(key, nca, foff, length, upper, out):
    subprocess.run([HELPER, "ctrfile", key, nca, hex(foff), hex(foff), hex(length), out, upper],
                   capture_output=True, check=True)


def main():
    if len(sys.argv) < 6:
        raise SystemExit(__doc__)
    keys, patch_tik, base_tik, patch_nca, base_nca = sys.argv[1:6]
    outdir = sys.argv[6] if len(sys.argv) > 6 else "/w/data/romfs"

    def content_key(tik):
        d = open(tik, "rb").read()
        return ecbdec(key_lookup(keys, "titlekek_%02x" % (d[0x285] - 1)), d[0x180:0x190].hex())

    ck_patch, ck_base = content_key(patch_tik), content_key(base_tik)
    print("content keys derived")

    base_sparse = os.path.join(OBJ, "base_sparse.bin")
    ctrfile(ck_base, base_nca, BASE_SPARSE_OFF, BASE_SPARSE_SIZE, BASE_SPARSE_UPPER, base_sparse)
    reloc = os.path.join(OBJ, "reloc.bin")
    enc = os.path.join(OBJ, "enc.bin")
    ctrfile(ck_patch, patch_nca, PATCH_RELOC_OFF, PATCH_RELOC_SIZE, PATCH_UPPER, reloc)
    ctrfile(ck_patch, patch_nca, PATCH_ENC_OFF, PATCH_ENC_SIZE, PATCH_UPPER, enc)
    print("bucket trees decrypted")

    merged = os.path.join(OBJ, "merged.bin")
    r = subprocess.run([MERGE, patch_nca, base_nca, ck_patch, ck_base,
                        reloc, enc, base_sparse, merged])
    if r.returncode != 0:
        raise SystemExit("merge failed")

    os.makedirs(outdir, exist_ok=True)
    r = subprocess.run(["python3", "/w/scripts/romfs_extract.py", merged,
                        hex(ROMFS_OFFSET), outdir])
    if r.returncode != 0:
        raise SystemExit("romfs extraction failed")

    os.unlink(merged)
    for p in (base_sparse, reloc, enc):
        os.unlink(p)
    print("done")


if __name__ == "__main__":
    main()
