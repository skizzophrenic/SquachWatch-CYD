#!/usr/bin/env bash
# Every WiFi scan in this tree must be a PASSIVE scan unless a human said
# otherwise, in writing, on the line.
#
# Why this exists: Arduino's WiFi.scanNetworks() takes `passive` as its THIRD
# argument and defaults it to false, so the short, innocent-looking call
#
#     WiFi.scanNetworks(true /* async */);
#
# is an ACTIVE scan. An active scan transmits a probe request on every channel
# with the board's own MAC in it. The manual WIFI SCAN screen did exactly that
# from the day it shipped: a surveillance detector broadcasting its presence on
# all thirteen channels, while the FAQ's "here's everything that goes out" table
# said the WiFi side only listens. Nothing warns you -- it compiles, it runs,
# it even finds networks. The only way to notice is to count the arguments.
#
# So: count them here, on every push, instead of hoping somebody counts them in
# review. If a scan genuinely needs to probe, put SQW_ACTIVE_SCAN_OK on the
# line (or just above it) with a reason, and this will leave it alone -- the
# point is that active scanning becomes a thing you declare, not a thing you
# get by typing fewer arguments.
#
# Keep this bash-3.2 clean: that is what macOS ships, and a contributor on a
# Mac should be able to run it before opening a PR.
#
# Usage: tools/check_emissions.sh     (from the repo root; exits non-zero on a
#                                      finding, prints file:line for each)

set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root" || exit 2

fail=0
marker='SQW_ACTIVE_SCAN_OK'

# Does the hit line, or the comment block above it, carry the marker? Eight
# lines of lookback, because a reason worth writing down rarely fits in one
# and the marker belongs on the first line of the explanation, not the last.
declared() {
    _f="$1"; _l="$2"
    _from=$(( _l - 8 )); [ "$_from" -lt 1 ] && _from=1
    sed -n "${_from},${_l}p" "$_f" | grep -q "$marker"
}

report() {
    printf '%s:%s: %s\n' "$1" "$2" "$3" >&2
    fail=1
}

# The line with its comments taken off, so that prose ABOUT a scan is not
# mistaken for a scan. Several of the real ones are discussed in comments
# right next to code that does something else, and an audit nobody can get to
# zero is an audit everybody learns to ignore.
code_of() {
    sed -n "${2}p" "$1" | sed -e 's;//.*;;' -e 's;/\*[^*]*\*/; ;g' -e 's;^[[:space:]]*\*.*;;'
}

# 1. Arduino's scanNetworks(): the third argument must be there and must be
#    true. `grep -o` keeps this to the call itself, so a long line's trailing
#    comment cannot accidentally satisfy the check.
while IFS=: read -r f l _rest; do
    [ -n "${f:-}" ] || continue
    call=$(code_of "$f" "$l")
    case "$call" in
        *'scanNetworks('*) ;;
        *) continue ;;
    esac
    case "$call" in
        *'scanNetworks()'*)
            declared "$f" "$l" || report "$f" "$l" \
                'scanNetworks() with no arguments is an ACTIVE scan (it probes). Pass passive=true, or declare SQW_ACTIVE_SCAN_OK with a reason.'
            ;;
        *)
            # Third positional argument, however it is spelled/commented.
            args=$(printf '%s\n' "$call" | sed -e 's/.*scanNetworks(//' -e 's/).*//' -e 's;/\*[^*]*\*/;;g')
            third=$(printf '%s\n' "$args" | awk -F, '{ gsub(/[ \t]/, "", $3); print $3 }')
            if [ "$third" != "true" ]; then
                declared "$f" "$l" || report "$f" "$l" \
                    'scanNetworks() without passive=true is an ACTIVE scan (it probes). Pass passive=true, or declare SQW_ACTIVE_SCAN_OK with a reason.'
            fi
            ;;
    esac
done <<EOF
$(grep -rn 'WiFi\.scanNetworks(' src include 2>/dev/null)
EOF

# 2. The IDF path, where the scan type is set by hand.
while IFS=: read -r f l _rest; do
    [ -n "${f:-}" ] || continue
    case "$(code_of "$f" "$l")" in
        *'WIFI_SCAN_TYPE_ACTIVE'*) ;;
        *) continue ;;
    esac
    declared "$f" "$l" || report "$f" "$l" \
        'WIFI_SCAN_TYPE_ACTIVE transmits probe requests. Use WIFI_SCAN_TYPE_PASSIVE, or declare SQW_ACTIVE_SCAN_OK with a reason.'
done <<EOF
$(grep -rn 'WIFI_SCAN_TYPE_ACTIVE' src include 2>/dev/null)
EOF

if [ "$fail" -ne 0 ]; then
    echo '' >&2
    echo 'check_emissions: a WiFi scan in this tree transmits. See the notes at the top of tools/check_emissions.sh.' >&2
    exit 1
fi

echo 'check_emissions: every WiFi scan is passive, or says in writing why it is not.'
