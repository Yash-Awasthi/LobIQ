#!/usr/bin/env bash
# The hft command end to end on a synthetic day: listing, replay, backtest with a report, study.
set -euo pipefail
export HFT_BIN=$1 HFT_DATA
HFT_DATA=$(mktemp -d)
trap 'rm -rf "$HFT_DATA"' EXIT
hft=$(dirname "$0")/../scripts/lobiq
"$hft" help | grep "lobiq live" >/dev/null
"$(dirname "$0")/../scripts/hft" help | grep "lobiq live" >/dev/null  # the former name still works
rc=0; "$hft" bogus 2>/dev/null || rc=$?; [ $rc -eq 2 ]
rc=0; "$hft" replay S010101 2>/dev/null || rc=$?; [ $rc -eq 1 ]  # no such store
"$HFT_BIN/gen_day" 1 120 2>/dev/null | "$HFT_BIN/ingest" "$HFT_DATA/store-S010125-v50" >/dev/null 2>&1
"$hft" days | grep "S010125    2025-01-01" >/dev/null
"$hft" replay 2025-01-01 | grep "symbols, .* 0 check failures" >/dev/null
"$hft" backtest S010125 QRSIM naive >/dev/null 2>&1
ls "$HFT_DATA"/reports/backtest-*/report.html >/dev/null
"$hft" strategies | grep avellaneda_stoikov >/dev/null
"$hft" bestex 2025-01-01 | grep '^## By symbol' >/dev/null
"$hft" surveil 2025-01-01 | grep '^## Summary' >/dev/null
"$hft" book 2025-01-01 QRSIM 10:00:00 3 | grep '^QRSIM at 10:00:00' >/dev/null
"$hft" leadlag 2025-01-01 QRSIM:QRSIM | grep '^| QRSIM -> QRSIM | 1 |' >/dev/null
"$hft" markov 2025-01-01 QRSIM | grep '^## Prediction, out of sample' >/dev/null
"$hft" markov 2025-01-01 QRSIM --json | grep '"test":{"moves":' >/dev/null
"$hft" mlofi 2025-01-01 QRSIM --interval 60 --split 10:30 | grep '^## Price impact, out of sample' >/dev/null
"$hft" lobster 2025-01-01 QRSIM "$HFT_DATA/lob" --levels 3 | grep '^QRSIM' >/dev/null
ls "$HFT_DATA"/lob/QRSIM_2025-01-01_34200000_57600000_orderbook_3.csv >/dev/null
"$hft" realism 2025-01-01 QRSIM 2025-01-01 QRSIM | grep '^| \*\*mean\*\* | | | \*\*0.000\*\*' >/dev/null
"$hft" realism 2025-01-01 QRSIM | grep '^## Stylised facts' >/dev/null
echo "hft cli ok"
