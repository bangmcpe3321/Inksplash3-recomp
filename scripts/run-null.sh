#!/usr/bin/env bash
# Run the headless (null backend) binary and keep the log in WORK/logs/.
#
# Host mode re-enters the build container; in-container mode runs the binary directly.
#
# Paths (override with the environment):
#   TOTK_IMG    decompressed NSO images (main.bin, sdk.bin, ...)   default /w/img
#   TOTK_ROMFS  extracted romfs                                    default /w/base-nsp/romfs
#   TIMEOUT     seconds before SIGTERM (then SIGKILL after 5 s)    default 240
#
# The Splat3 tree has no extracted image yet, so both defaults are placeholders: set them
# once the NSOs are unpacked (tools/nso.py, see SPLAT3_PLAN.md step 1). Nothing here writes
# to the repository -- the log and the save dir stay under WORK/logs and WORK/userdata.
set -euo pipefail

IMAGE=${IMAGE:-localhost/engage-linux-build:latest}
BIN=${BIN:-/w/build-null/totk}
OUT=${OUT:-/w/build-null}

TOTK_IMG=${TOTK_IMG:-/w/data/img}
TOTK_ROMFS=${TOTK_ROMFS:-/w/data/romfs}
TIMEOUT=${TIMEOUT:-240}
# TOTK_LIFT_ALL=1 binds main's unimplemented imports to the lifted sdk bodies (see
# UPSTREAM_DIFF.md); the TOTK_LIFT service exclusion list still applies.
LIFT_ALL=${TOTK_LIFT_ALL:-1}
EXTRA_ARGS=${EXTRA_ARGS:-}
TOTK_NVN_INT=${TOTK_NVN_INT:-}

inner() {
  if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built (run port/scripts/build-null.sh first)" >&2
    exit 1
  fi
  mkdir -p /w/logs /w/userdata
  n=1; while [ -e "/w/logs/run-null-$n.log" ]; do n=$((n + 1)); done
  log=/w/logs/run-null-$n.log
  echo "+ TOTK_IMG=$TOTK_IMG TOTK_ROMFS=$TOTK_ROMFS TOTK_HEADLESS=1 TOTK_LIFT_ALL=$LIFT_ALL timeout -k 5 $TIMEOUT $BIN"
  echo "+ log -> $log"
  set +e
  ( cd "$OUT" && \
    TOTK_IMG="$TOTK_IMG" TOTK_ROMFS="$TOTK_ROMFS" TOTK_HEADLESS=1 TOTK_LIFT_ALL="$LIFT_ALL" \
    TOTK_USERDATA=/w/userdata \
    timeout -k 5 "$TIMEOUT" "$BIN" $EXTRA_ARGS ) >"$log" 2>&1
  rc=$?
  set -e
  # a headless run ends by timeout (124) or by the game; anything else is worth looking at
  echo "exit $rc after $(grep -c . "$log" || true) log lines; last 20:"
  tail -n 20 "$log"
  exit 0
}

if [ -d /w/port/Splat3-Recomp/baseline/totk-recomp ]; then
  inner
  exit $?
fi

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
host_work=$(cd "$here/../../.." && pwd)
exec podman run --rm \
  -e BIN="$BIN" -e OUT="$OUT" -e TOTK_IMG="$TOTK_IMG" -e TOTK_ROMFS="$TOTK_ROMFS" \
  -e TIMEOUT="$TIMEOUT" -e TOTK_LIFT_ALL="$LIFT_ALL" -e EXTRA_ARGS="$EXTRA_ARGS" -e TOTK_NVN_INT="$TOTK_NVN_INT" \
  -v "$host_work:/w:z" -w /w/port/Splat3-Recomp/baseline/totk-recomp "$IMAGE" \
  bash "/w/port/Splat3-Recomp/scripts/$(basename "${BASH_SOURCE[0]}")" "$@"
