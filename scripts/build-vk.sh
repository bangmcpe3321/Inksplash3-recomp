#!/usr/bin/env bash
# Build the Vulkan (windowed) binary for the Splat3 port tree.
#
# Same two modes as build-null.sh (host -> container, or in-container directly).
#   build-vk.sh              -> /w/build-vk/totk
#   IMAGE=... build-vk.sh    -> another build image
#
# Extra ninja args are passed through (e.g. `build-vk.sh -j 6`).
# Run it from the host with no extra podman flags; a *game window* run additionally needs
# --device /dev/dri and the wayland/X sockets -- see run-null.sh's header, or the Splat2
# HANDOFF.md "Run" section.
set -euo pipefail

IMAGE=${IMAGE:-localhost/engage-linux-build:latest}
GEN=${GEN:-/w/gen/main}
OBJ=${OBJ:-/w/obj}
DEPS=${DEPS:-/w/deps}
SYSLIB=${SYSLIB:-/w/syslib}
SYSINC=${SYSINC:-/w/deps/libshaderc/include}
OUT=${OUT:-/w/build-vk}
POOL=${POOL:-4}

# clang-19 / clang++-19 do not exist in this image; mkninja_linux.py defaults to them.
export CC=clang CXX=clang++

inner() {
  echo "+ python3 tools/recomp/mkninja_linux.py --root /w/port/Splat3-Recomp/baseline/totk-recomp --gen $GEN --obj $OBJ \\"
  echo "      --deps $DEPS --syslib $SYSLIB --sysinc $SYSINC --gen-opt -O1 --gen-pool $POOL --out $OUT"
  python3 tools/recomp/mkninja_linux.py \
      --root /w/port/Splat3-Recomp/baseline/totk-recomp \
      --gen "$GEN" --obj "$OBJ" --deps "$DEPS" \
      --syslib "$SYSLIB" --sysinc "$SYSINC" \
      --out "$OUT" --gen-opt -O1 --gen-pool "$POOL"
  echo "+ ninja -C $OUT $*"
  ninja -C "$OUT" "$@"
}

if [ -d /w/port/Splat3-Recomp/baseline/totk-recomp ]; then
  cd /w/port/Splat3-Recomp/baseline/totk-recomp
  inner "$@"
  exit $?
fi

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
