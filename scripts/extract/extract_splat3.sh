#!/usr/bin/env bash
# extract_splat3.sh - reproduce the Splatoon 3 v11.3.0 (update) extraction pipeline.
#
# Everything runs inside the podman image localhost/engage-linux-build:latest.
# WORK is mounted at /w, the game tree at /src (read-only).
#
# Steps
#   1. PFS0-unpack the base NSP and the v11.3.0 update NSP          -> data/nsp/, obj/{base,upd}
#   2. build hactool 1.4.0 + its bundled mbedtls                    -> deps/hactool/hactool
#   3. build the small helpers (crypt/sparse/BKTR tools)            -> obj/
#   4. filter prod.keys, build title.keys for base+patch            (chmod 600, deleted later)
#   5. ExeFS: LibHac 0.19 extract of the update Code section, then decompress every NSO
#             with hactool -t nso --uncompressed + NPDM JSON dump
#   6. RomFS: decrypt the BKTR relocation/encryption trees + the base sparse table,
#             merge patch+base into one 10.08 GB virtual image, extract the RomFS tree
#   7. verify (top-level dirs, file/size totals, zstd->SARC/FRES spot checks)
#
# The update's RomFS section is AesCtrEx (BKTR INDIRECT) over the base RomFS, and the base
# Program NCA is a "stub" (empty ExeFS + sparse holes in the romfs meta tables), so both
# NSPs are needed. See data/EXTRACT_REPORT.md.
#
# Usage: Work/scripts/extract_splat3.sh
set -euo pipefail

ROOT="/mnt/SSD Storage/Games/Ports/Switch/Splat3"
WORK="$ROOT/Work"
BASE_NSP="Splatoon 3 [0100C2500FC20000][USA][v65536].nsp"
UPD_NSP="Splatoon 3 [0100C2500FC20800][v2752512][Up v11.3.0].nsp"
IMG="localhost/engage-linux-build:latest"
CTR="splat3ext"

podman_exec() { podman exec "$CTR" bash -lc "$*"; }

mkdir -p "$WORK"/{data/nsp,data/exefs,data/romfs,deps/hactool,logs,obj,scripts}
if ! podman container exists "$CTR" 2>/dev/null; then
  podman run -d --name "$CTR" -v "$WORK":/w -v "$ROOT":/src:ro "$IMG" sleep infinity >/dev/null
fi
podman start "$CTR" >/dev/null 2>&1 || true

echo "=== [1/7] PFS0 unpack (base + update)"
podman_exec "python3 /w/scripts/pfs0_extract.py '/src/NSPs/$BASE_NSP' /w/obj/base | tee /w/logs/pfs0_base.log | tail -2"
podman_exec "python3 /w/scripts/pfs0_extract.py '/src/NSPs/$UPD_NSP'  /w/obj/upd  | tee /w/logs/pfs0_update.log | tail -2"

echo "=== [2/7] hactool build"
podman_exec "bash /w/scripts/build_hactool.sh"

echo "=== [3/7] helpers"
podman_exec "set -e
  INC=/w/deps/hactool/hactool-src/mbedtls/include
  LIB=/w/deps/hactool/hactool-src/mbedtls/library
  for t in crypt_helper desparse scan_node nca_probe find_nonce scan_bucket; do
      gcc -O2 -I \$INC -o /w/obj/\$t /w/scripts/\$t.c -L \$LIB -lmbedcrypto
  done
  gcc -O2 -I \$INC -o /w/obj/merge_bktr /w/scripts/merge_bktr.c -L \$LIB -lmbedcrypto"

echo "=== [4/7] keys"
podman_exec "python3 /w/scripts/filter_keys.py /src/prod.keys /w/obj/upd/keys_filtered.txt
  chmod 600 /w/obj/upd/keys_filtered.txt
  python3 /w/scripts/make_title_keys.py /w/obj/upd/keys_filtered.txt /w/obj/upd/title.keys \
      /w/obj/base/0100c2500fc20000000000000000000e.tik \
      /w/obj/upd/0100c2500fc20800000000000000000e.tik
  chmod 600 /w/obj/upd/title.keys"

echo "=== [5/7] ExeFS + NSO decompression + NPDM"
podman_exec "set -e
  NCA=/w/obj/upd/b17c38424e33cec90d4407ef33c7848d.nca
  /w/deps/ncaextract/bin/Release/net8.0/ncaextract /w/obj/upd/keys_filtered.txt \
      /w/obj/upd/title.keys \$NCA /w/data/exefs - | tail -3
  for f in main sdk rtld; do
      /w/deps/hactool/hactool -t nso --uncompressed=/w/data/exefs/\$f.decompressed /w/data/exefs/\$f >/dev/null
  done
  python3 /w/scripts/nso_info.py /w/data/exefs/main /w/data/exefs/sdk /w/data/exefs/rtld
  /w/deps/hactool/hactool -t npdm --json=/w/logs/main.npdm.json /w/data/exefs/main.npdm >/dev/null"

echo "=== [6/7] RomFS: decrypt trees + merge + extract"
podman_exec "set -e
  python3 /w/scripts/upd_romfs.py /w/obj/upd/keys_filtered.txt \
      /w/obj/upd/0100c2500fc20800000000000000000e.tik \
      /w/obj/base/0100c2500fc20000000000000000000e.tik \
      /w/obj/upd/b17c38424e33cec90d4407ef33c7848d.nca \
      /w/obj/base/af15ab60a7a03a634ae69a48e3ea2a14.nca"

echo "=== [7/7] verification"
podman_exec "set -e
  cd /w/data/romfs
  echo \"top-level dirs: \$(ls | wc -l)  files: \$(find . -type f | wc -l)  bytes: \$(find . -type f -printf '%s\n' | awk '{s+=\$1} END {print s}')\"
  for d in Layout Model Sound System Pack; do echo \"  \$d: \$(find \$d -type f | wc -l) files\"; done
  echo -n '  Params.pack.zs -> '; zstd -dc Pack/Params.pack.zs | head -c4; echo"

echo
echo "done. Report: /w/data/EXTRACT_REPORT.md"
