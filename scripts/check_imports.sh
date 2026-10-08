#!/bin/sh
# check_imports.sh ELF ALLOWLIST
#
# Fails when the console ELF needs a module or imports a symbol that the
# 1.0.2 release did not. The ps5-payload-sdk runtime loads every DT_NEEDED
# module and binds every import before main(); any failure there ends the
# payload with no notification and no log, so new symbols have to be looked
# up at run time instead. NEEDED must be 1.0.2's list, in the same order,
# minus modules that are now loaded at run time.
set -eu
elf=$1
allow=$2
READELF=${READELF:-readelf}
command -v "$READELF" >/dev/null 2>&1 || READELF=llvm-readelf
tmp=${TMPDIR:-/tmp}/hb_imports.$$
trap 'rm -f "$tmp".*' EXIT INT TERM

sed -n 's/^needed //p' "$allow" > "$tmp.need_ok"
grep -v '^#' "$allow" | grep -v '^needed ' | sed '/^$/d' | sort -u > "$tmp.sym_ok"
"$READELF" -d "$elf" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' > "$tmp.need"
"$READELF" --dyn-syms -W "$elf" |
    awk '$7 == "UND" && $8 != "" { sub(/@.*/, "", $8); print $8 }' | sort -u > "$tmp.sym"

status=0
# ordered subset: every NEEDED entry appears in 1.0.2's list, in order
if ! awk 'NR == FNR { ok[++n] = $0; next } { while (i < n && ok[++i] != $0) ; if (ok[i] != $0) bad = 1 }
          END { exit bad }' "$tmp.need_ok" "$tmp.need"; then
    echo "check_imports: DT_NEEDED not an ordered subset of 1.0.2's (want, got):" >&2
    diff "$tmp.need_ok" "$tmp.need" >&2 || true
    status=1
fi
new=$(comm -13 "$tmp.sym_ok" "$tmp.sym")
if [ -n "$new" ]; then
    echo "check_imports: imports 1.0.2 did not have (resolve them at run time):" >&2
    echo "$new" | sed 's/^/  /' >&2
    status=1
fi
[ $status -eq 0 ] && echo "check_imports: $(wc -l < "$tmp.sym") imports, NEEDED $(tr "\n" " " < "$tmp.need")within 1.0.2: ok"
exit $status
