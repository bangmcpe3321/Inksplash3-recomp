#!/usr/bin/env bash
# Regenerate one module into a scratch dir, then copy only the changed TUs into the live tree.
#   scripts/regen.sh main|sdk
# Runs inside the build container (WORK mounted at /w). A changed generator + a ~10-TU rebuild instead of 600.
set -euo pipefail
mod=${1:?module}
W=/w
PY=${PY:-$W/toolchain/venv/bin/python}
ROOT=$W/port/Splat3-Recomp/baseline/totk-recomp
case "$mod" in
  main)    extra=() ;;
  sdk)     extra=(--base 0x7180000000 --prefix s --plt-all) ;;
  subsdk0) extra=(--base 0x7200000000 --prefix v --plt-all) ;;
  subsdk1) extra=(--base 0x7280000000 --prefix w --plt-all) ;;
  subsdk2) extra=(--base 0x7300000000 --prefix y --plt-all) ;;
  subsdk3) extra=(--base 0x7380000000 --prefix z --plt-all) ;;
  *) echo "unknown module $mod" >&2; exit 2 ;;
esac
rm -rf "$W/gen.new/$mod"
$PY "$ROOT/tools/recomp/gen.py" "$W/data/img/$mod" "$W/data/funcs/funcs_$mod.json" "$W/gen.new/$mod" "${extra[@]}" \
    > "$W/logs/gen_$mod.log" 2>&1
$PY "$W/port/Splat3-Recomp/scripts/gen_sync.py" "$W/gen.new/$mod" "$W/gen/$mod"
rm -rf "$W/gen.new"
$PY - "$W/gen/$mod/report.json" "$mod" <<'EOF'
import json,sys
r=json.load(open(sys.argv[1]))
print(f"{sys.argv[2]}: functions {r['functions']} insns {r['insns']} unhandled {r['unhandled_total']}")
for k,v in r['unhandled'][:8]: print(f"  {v:5d} {k}")
EOF
