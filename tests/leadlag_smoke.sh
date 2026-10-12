#!/usr/bin/env bash
# itch_leadlag end to end on a synthetic day: a symbol against itself must find its moves and the
# 2 s placebo must sit near 50%.
set -euo pipefail
apps=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$apps/gen_day" 3 390 2>/dev/null | "$apps/ingest" "$tmp/day" >/dev/null 2>&1
row=$("$apps/itch_leadlag" "$tmp/day" QRSIM:QRSIM | grep -m1 '^| QRSIM -> QRSIM | 1 |')
echo "$row"
echo "$row" | awk -F'|' '{gsub(/[% ]/, "", $6); gsub(/[% ]/, "", $7)} $4 > 1000 && $7 > 40 && $7 < 60 {ok=1} END{exit !ok}'
