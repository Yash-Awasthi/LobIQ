#!/usr/bin/env bash
# itch_study end to end on two synthetic days: OFI must explain price changes and the micro-price
# must beat the mid out of sample (queue-reactive flow has both effects built in).
set -euo pipefail
apps=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for s in 1 2; do "$apps/gen_day" $s 390 | "$apps/ingest" "$tmp/day$s" >/dev/null 2>&1; done
out=$(THREADS=2 "$apps/itch_study" --train "$tmp/day1" --test "$tmp/day2" --symbols 1)
ofi=$(echo "$out" | awk -F'|' '/^\| QRSIM \| [0-9]+ \| [0-9.]+ \|/{print $6; exit}')
micro=$(echo "$out" | awk -F'|' '/^\| QRSIM \| 1.00 \|/{print $6; exit}' | tr -d ' %+')
echo "OFI R2 test $ofi, micro-price next-change gain $micro%"
awk -v r="$ofi" -v m="$micro" 'BEGIN{exit !(r > 0.3 && m > 0)}'
