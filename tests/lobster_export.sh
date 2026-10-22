#!/usr/bin/env bash
# itch_lobster on a synthetic day, checked by rebuilding the book from the message file alone.
set -euo pipefail
B=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$B/gen_day" 3 30 2>/dev/null | "$B/ingest" "$tmp/store-S010225-v50" >/dev/null 2>&1
"$B/itch_lobster" "$tmp/store-S010225-v50" QRSIM "$tmp/out" --levels 5 --from 09:29 >/dev/null
f="$tmp/out/QRSIM_2025-01-02_34140000_57600000"
python3 "$(dirname "$0")/lobster_check.py" "${f}_message_5.csv" "${f}_orderbook_5.csv" 5
