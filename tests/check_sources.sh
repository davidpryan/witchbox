#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The chip has no FPU and no libgcc: no float types, no float literals, no 64-bit division in the WitchBox sources.
# (The firmware build catches the division at link time; this catches it before the toolchain is even installed.)
set -e
cd "$(dirname "$0")/.."
status=0
for f in firmware/src/witchbox.c firmware/src/wb/*.h; do
    code=$(sed -e 's,/\*.*\*/,,g' -e 's,//.*$,,' "$f" | awk 'BEGIN{c=0} { line=$0; if (c) { if (sub(/.*\*\//, "", line)) c=0; else next } while (match(line, /\/\*/)) { rest=substr(line, RSTART); if (sub(/\/\*.*\*\//, "", line)) continue; line=substr(line, 1, RSTART-1); c=1; break } print line }')
    if printf '%s\n' "$code" | grep -nE '\b(float|double)\b' >/dev/null; then
        echo "$f: float type:"; printf '%s\n' "$code" | grep -nE '\b(float|double)\b'; status=1
    fi
    if printf '%s\n' "$code" | grep -nE '[^A-Za-z_0-9][0-9]+\.[0-9]+' | grep -v '"' >/dev/null; then
        echo "$f: float literal:"; printf '%s\n' "$code" | grep -nE '[^A-Za-z_0-9][0-9]+\.[0-9]+' | grep -v '"'; status=1
    fi
    if printf '%s\n' "$code" | grep -nE '\(u?int64_t\)[^;]*[/%]' >/dev/null; then
        echo "$f: possible 64-bit division (check by hand):"; printf '%s\n' "$code" | grep -nE '\(u?int64_t\)[^;]*[/%]'
    fi
done
[ $status = 0 ] && echo "sources: integers only"
exit $status
