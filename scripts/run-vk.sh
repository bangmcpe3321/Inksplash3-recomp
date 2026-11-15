#!/usr/bin/env bash
# Run the Vulkan build with a real window. Talks to the host display, so tell the user first.
#   scripts/run-vk.sh
# Host env passed through: WAYLAND_DISPLAY, DISPLAY, XAUTHORITY, TOTK_PRESS, TOTK_PC_INPUT, TOTK_NVN_INT.
set -euo pipefail
IMAGE=${IMAGE:-localhost/engage-linux-build:latest}
BIN=${BIN:-/w/build-vk/totk}
OUT=${OUT:-/w/build-vk}
TIMEOUT=${TIMEOUT:-180}
LIFT_ALL=${TOTK_LIFT_ALL:-1}
UIDH=${UIDH:-1000}

inner() {
  [ -x "$BIN" ] || { echo "error: $BIN not built (scripts/build-vk.sh)" >&2; exit 1; }
  mkdir -p /w/logs
  n=1; while [ -e "/w/logs/vk-$n.log" ]; do n=$((n + 1)); done
  log=/w/logs/vk-$n.log
  echo "+ log -> $log"
  set +e
  ( cd "$OUT" && \
    TOTK_IMG=/w/data/img TOTK_ROMFS=/w/data/romfs TOTK_LIFT_ALL="$LIFT_ALL" \
    TOTK_PRESS="${TOTK_PRESS:-}" TOTK_PC_INPUT="${TOTK_PC_INPUT:-1}" TOTK_NVN_INT="${TOTK_NVN_INT:-}" \
    TOTK_SHOT_EVERY="${TOTK_SHOT_EVERY:-}" TOTK_SHOT_RANGE="${TOTK_SHOT_RANGE:-}" \
    timeout -k 5 "$TIMEOUT" "$BIN" ) >"$log" 2>&1
  rc=$?
  set -e
  echo "exit $rc; last 15 lines:"
  tail -n 15 "$log"
}

if [ -d /w/port/Splat3-Recomp/baseline/totk-recomp ]; then inner; exit $?; fi

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
host_work=$(cd "$here/../../.." && pwd)
exec podman run --rm --device /dev/dri \
  --mount type=bind,source=/run/user/$UIDH,destination=/run/user/$UIDH \
  -v /dev/input:/dev/input -v /run/udev:/run/udev:ro \
  -e XDG_RUNTIME_DIR=/run/user/$UIDH \
  -e WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}" -e DISPLAY="${DISPLAY:-:0}" \
  -e TOTK_PRESS="${TOTK_PRESS:-}" -e TOTK_PC_INPUT="${TOTK_PC_INPUT:-1}" \
  -e TOTK_NVN_INT="${TOTK_NVN_INT:-}" -e TOTK_SHOT_EVERY="${TOTK_SHOT_EVERY:-}" -e TOTK_SHOT_RANGE="${TOTK_SHOT_RANGE:-}" \
  -e TIMEOUT="$TIMEOUT" -e TOTK_LIFT_ALL="$LIFT_ALL" -e LARECOMP_NO_SHADOW="${LARECOMP_NO_SHADOW:-}" -e LARECOMP_HITCH_MS="${LARECOMP_HITCH_MS:-}" \
  -e LARECOMP_VK_NO_HEAP_MIRROR="${LARECOMP_VK_NO_HEAP_MIRROR:-}" -e TOTK_PROF="${TOTK_PROF:-}" \
  -e LARECOMP_VK_PROF="${LARECOMP_VK_PROF:-}" -e LARECOMP_VK_PRESENT="${LARECOMP_VK_PRESENT:-}" \
  -e TOTK_RES_SCALE="${TOTK_RES_SCALE:-}" -e TOTK_OUT_SCALE="${TOTK_OUT_SCALE:-}" -e TOTK_AA="${TOTK_AA:-}" \
  -v "$host_work:/w:z" -w /w/build-vk "$IMAGE" \
  bash "/w/port/Splat3-Recomp/scripts/$(basename "${BASH_SOURCE[0]}")" "$@"
