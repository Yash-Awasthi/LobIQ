#!/usr/bin/env bash
# itch_book end to end: on a synthetic day stored in small chunks with checkpoints, the book at any
# time must be the same whether built from the nearest checkpoint or replayed from the open.
set -euo pipefail
apps=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$apps/gen_day" 5 390 2>/dev/null | CHUNK_MSGS=20000 "$apps/ingest" "$tmp/day" >/dev/null 2>&1
CHECKPOINT_EVERY=40000 "$apps/checkpoint" "$tmp/day" >/dev/null
used=0
for t in 09:45:00 11:11:11.5 13:00:00.000123 15:59:59.999999999; do
    a=$("$apps/itch_book" "$tmp/day" QRSIM "$t" 20)
    b=$("$apps/itch_book" "$tmp/day" QRSIM "$t" 20 --no-checkpoint)
    [ "${a%%; replayed*}" = "${b%%; replayed*}" ] || { echo "books differ at $t"; exit 1; }
    [ "$(echo "$a" | tail -n +2)" = "$(echo "$b" | tail -n +2)" ] || { echo "levels differ at $t"; exit 1; }
    case $a in *"from the checkpoint"*) used=$((used + 1)) ;; esac
done
[ $used -ge 3 ] || { echo "checkpoints used at only $used of 4 times"; exit 1; }
"$apps/itch_book" "$tmp/day" NOPE 10:00:00 >/dev/null 2>&1 && exit 1
"$apps/itch_book" "$tmp/day" QRSIM 25:00:00 >/dev/null 2>&1 && exit 1
echo "book queries agree; checkpoints used at $used of 4 times"
