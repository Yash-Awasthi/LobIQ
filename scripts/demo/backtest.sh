#!/usr/bin/env bash
# Demo (e): a synthetic Nasdaq session (gen_day | ingest) through two strategies and a parameter sweep,
# HTML reports for each. Totals are fixed by the seed; exit 1 if one differs. Usage: backtest.sh [out-dir]
set -euo pipefail
cd "$(dirname "$0")/../.."
B=${HFT_BIN:-build/release/apps}
out=${1:-out/backtest-demo}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$B/gen_day" 7 390 2>/dev/null | "$B/ingest" "$tmp/store" >/dev/null
venue="maker_rebate=2000 taker_fee=3000 market_data_ns=50000 order_entry_ns=20000"
fail=0
for run in "naive 5610.80" "imbalance 5500.90"; do
  set -- $run
  got=$("$B/itch_backtest" "$tmp/store" QRSIM "$1" $venue --report "$out/$1" 2>/dev/null | awk '$1 == "total_usd" {print $2}')
  echo "$1: total_usd $got, report $out/$1/report.html"
  [ "$got" = "$2" ] || { echo "  expected $2"; fail=1; }
done
best=$("$B/itch_backtest" "$tmp/store" QRSIM imbalance $venue --sweep threshold=0.2,0.4,0.6,0.8,1 --sweep size=100,200 \
       --report "$out/sweep" 2>/dev/null | awk 'NR > 1 && ($3 > m || NR == 2) {m = $3} END {print m}')
echo "sweep: 10 runs, best total_usd $best, report $out/sweep/index.html"
[ "$best" = "8551.50" ] || { echo "  expected 8551.50"; fail=1; }
exit $fail
