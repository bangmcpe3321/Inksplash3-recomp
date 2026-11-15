#!/usr/bin/env bash
# Build the headless (null GPU backend) binary for the Splat3 port tree.
#
# Works in two modes:
#   * on the host  -> re-executes itself inside the build container, mounting WORK at /w
#   * in the container -> runs the generator/ninja commands directly (paths are /w/...)
#
# The tree lives under a host path with spaces ("/mnt/SSD Storage/..."), so the container
# mount point is always /w and every path handed to the tools is a /w/... path.
#
#   build-null.sh            -> /w/build-null/totk
#   build-null.sh -j 8       -> extra ninja args are passed through
set -euo pipefail

IMAGE=${IMAGE:-localhost/engage-linux-build:latest}
WORK=${WORK:-/w}
GEN=${GEN:-/w/gen/main}
OBJ=${OBJ:-/w/obj}
DEPS=${DEPS:-/w/deps}
SYSLIB=${SYSLIB:-/w/syslib}
SYSINC=${SYSINC:-/w/deps/libshaderc/include}
OUT=${OUT:-/w/build-null}
POOL=${POOL:-4}

# clang-19 / clang++-19 do not exist in this image; mkninja_linux.py defaults to them.
export CC=clang CXX=clang++

inner() {
  echo "+ python3 tools/recomp/mkninja_linux.py --null --root /w/port/Splat3-Recomp/baseline/totk-recomp --gen $GEN --obj $OBJ \\"
  echo "      --deps $DEPS --syslib $SYSLIB --sysinc $SYSINC --gen-opt -O1 --gen-pool $POOL --out $OUT"
  python3 tools/recomp/mkninja_linux.py --null \
      --root /w/port/Splat3-Recomp/baseline/totk-recomp \
      --gen "$GEN" --obj "$OBJ" --deps "$DEPS" \
      --syslib "$SYSLIB" --sysinc "$SYSINC" \
      --out "$OUT" --gen-opt -O1 --gen-pool "$POOL"
  echo "+ ninja -C $OUT $*"
  ninja -C "$OUT" "$@"
}

# ---- inside the container: /w is the mount point of WORK
if [ -d /w/port/Splat3-Recomp/baseline/totk-recomp ]; then
  cd /w/port/Splat3-Recomp/baseline/totk-recomp
  inner "$@"
  exit $?
fi

# ---- on the host: find WORK (this script lives in WORK/port/scripts) and re-enter
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
host_work=$(cd "$here/../../.." && pwd)
if [ ! -d "$host_work/port/totk-recomp" ]; then
  echo "error: $host_work/port/totk-recomp not found (run this from WORK/port/scripts)" >&2
  exit 1
fi
exec podman run --rm \
  -e GEN="$GEN" -e OBJ="$OBJ" -e DEPS="$DEPS" -e SYSLIB="$SYSLIB" -e SYSINC="$SYSINC" \
  -e OUT="$OUT" -e POOL="$POOL" \
  -v "$host_work:/w:z" -w /w/port/Splat3-Recomp/baseline/totk-recomp "$IMAGE" \
  bash "/w/port/scripts/$(basename "${BASH_SOURCE[0]}")" "$@"
