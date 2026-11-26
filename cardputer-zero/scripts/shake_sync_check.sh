#!/bin/sh
# SquachWatch-CM0 -- keep the CM0 Shake in step with the firmware's.
#
# src/cm0/shake_iio.cpp carries three blocks of ../src/shake.cpp
# verbatim (the tuning constants, feed(), the end of poll()), and
# src/cm0/sys_shim/shake.h carries the API of ../include/shake.h's
# non-SQUACHSIM branch. After a firmware change this fails on any drift:
# copy the new firmware text in, then re-check on the device.
#
#   scripts/shake_sync_check.sh [firmware-dir]     (default: ..)
set -eu
FW=${1:-..}
CM0=src/cm0
fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# upstream_block NAME START-PREFIX END-LINE: from the first line starting
# with START-PREFIX to the first line after it equal to END-LINE (or, when
# END-LINE starts with '^', starting with the rest).
upstream_block() {
    awk -v s="$2" -v e="$3" '
        !on && index($0, s) == 1 { on = 1 }
        on { print }
        on && ((substr(e, 1, 1) == "^" && index($0, substr(e, 2)) == 1) || $0 == e) { exit }
    ' "$FW/src/shake.cpp" > "$tmp/$1.fw"
}
# cm0_block NAME: between "// ---- BEGIN upstream shake.cpp: NAME" and its END.
cm0_block() {
    awk -v n="$1" '
        index($0, "// ---- END upstream shake.cpp: " n) == 1 { on = 0 }
        on { print }
        index($0, "// ---- BEGIN upstream shake.cpp: " n) == 1 { on = 1 }
    ' "$CM0/shake_iio.cpp" > "$tmp/$2.cm0"
}
check() {
    if [ ! -s "$tmp/$1.fw" ] || ! diff -u "$tmp/$1.fw" "$tmp/$1.cm0" > "$tmp/$1.diff"; then
        echo "shake-sync-check: FAIL -- $1 differs from $FW (or was not found):"
        head -30 "$tmp/$1.diff" 2>/dev/null || true
        fail=1
    fi
}

upstream_block tuning 'const int32_t  EXC_MG' '^const uint32_t COOLDOWN_MS'
cm0_block tuning tuning
check tuning

upstream_block feed '// One sample, at time t' '}'
cm0_block feed feed
check feed

upstream_block tail '    if (s_trace && now - s_traceAt >= 1000)' '    return out;'
cm0_block 'poll tail' tail
check tail

# The API: the namespace block of the header's non-SQUACHSIM branch.
awk '/^#else/ { on = 1; next } on && /^namespace Shake/ { p = 1 } p { print } p && /^}/ { exit }' \
    "$FW/include/shake.h" > "$tmp/api.fw"
awk '/^namespace Shake/ { p = 1 } p { print } p && /^}/ { exit }' \
    "$CM0/sys_shim/shake.h" > "$tmp/api.cm0"
check api

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "shake-sync-check: ok (tuning, feed, poll tail and API match $FW)"
