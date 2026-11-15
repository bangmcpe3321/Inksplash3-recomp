#!/usr/bin/env bash
# Splatoon 3: exefs NSOs -> flat images -> function tables -> lifted C++.
# Runs INSIDE the build container with WORK mounted at /w.
#   podman run --rm -v "$PWD:/w:z" -w /w localhost/engage-linux-build:latest bash /w/port/Splat3-Recomp/scripts/gen_all.sh [modules...]
# Default modules: main sdk (the two the runtime actually loads). Pass more (subsdk0 ...) for coverage analysis.
set -euo pipefail
W=/w
PY=${PY:-$W/toolchain/venv/bin/python}
ROOT=$W/port/Splat3-Recomp/baseline/totk-recomp
EX=$W/data/exefs
IMG=$W/data/img
GEN=$W/gen
FUNC=$W/data/funcs
mkdir -p "$IMG" "$GEN" "$FUNC"
export PYTHONPATH="$ROOT/tools/recomp:${PYTHONPATH:-}"

mods=("$@"); [ ${#mods[@]} -eq 0 ] && mods=(main sdk)

for m in "${mods[@]}"; do
  [ -f "$EX/$m" ] || { echo "missing $EX/$m"; exit 1; }
  echo "== $m: image"
  $PY "$ROOT/tools/nso.py" "$EX/$m" "$IMG/$m" | tail -1
  echo "== $m: function table"
  $PY "$ROOT/tools/funcs.py" "$IMG/$m" "$FUNC/funcs_$m.json" | tail -1

  extra=()
  case "$m" in
    main)    extra=(--hook 7a83e4) ;;                 # TotK-specific hook; harmless placeholder for now
    sdk)     extra=(--base 0x7180000000 --prefix s --plt-all) ;;
    subsdk0) extra=(--base 0x7200000000 --prefix v --plt-all) ;;
    subsdk1) extra=(--base 0x7280000000 --prefix w --plt-all) ;;
    subsdk2) extra=(--base 0x7300000000 --prefix y --plt-all) ;;
    subsdk3) extra=(--base 0x7380000000 --prefix z --plt-all) ;;
    *)       echo "no base for $m"; exit 1 ;;
  esac
  echo "== $m: lift"
  $PY "$ROOT/tools/recomp/gen.py" "$IMG/$m" "$FUNC/funcs_$m.json" "$GEN/$m" "${extra[@]}" \
      > "$W/logs/gen_$m.log" 2>&1 || { tail -40 "$W/logs/gen_$m.log"; exit 1; }
  $PY - "$GEN/$m/report.json" "$m" <<'EOF'
import json,sys,collections
r=json.load(open(sys.argv[1])); u=r.get('unhandled',[])
print(f"{sys.argv[2]}: functions {r['functions']} insns {r['insns']} tus {r['tus']} unhandled {r['unhandled_total']}")
if u:
    c=collections.Counter(str(x[0]).split()[0] for x in u)
    print("  top unhandled:", ', '.join(f"{k} x{v}" for k,v in c.most_common(12)))
    for k,v in u[:5]: print(f"    {v:5d}  {k}")
EOF
done
echo "done -> $GEN"
