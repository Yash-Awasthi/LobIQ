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

# Accounting identities of the later sections, on every day row.
section() { echo "$out" | sed -n "/^## $1/,/^## [A-Z]/p" | grep -E '^\| day[0-9]'; }
section "Order lifetimes" | awk -F'|' '{s=0; for (i=4; i<=9; i++) s+=$i; s+=$11+$12; if (s<99.5||s>100.5) {print "lifetimes sum to " s; exit 1}}'
section "Trade costs" | awk -F'|' '{d=$5-($6+$7); if (d<-0.02||d>0.02) {print "effective != realized + impact: " $0; exit 1}}'
section "VPIN" | awk -F'|' '{if ($3<0||$3>1) {print "VPIN out of [0,1]: " $0; exit 1}}'
section "Self-excitation" | awk -F'|' '{split($4, a, " "); if (a[1]<0||a[1]>=1) {print "branching ratio out of [0,1): " $0; exit 1}}'
echo "identities hold"
