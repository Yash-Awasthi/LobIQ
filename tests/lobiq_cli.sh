#!/usr/bin/env bash
# The lobiq command end to end on a synthetic day: listing, replay, backtest with a report, study.
set -euo pipefail
export LOBIQ_BIN=$1 LOBIQ_DATA
LOBIQ_DATA=$(mktemp -d)
trap 'rm -rf "$LOBIQ_DATA"' EXIT
lobiq=$(dirname "$0")/../scripts/lobiq
"$lobiq" help | grep "lobiq live" >/dev/null
rc=0; "$lobiq" bogus 2>/dev/null || rc=$?; [ $rc -eq 2 ]
rc=0; "$lobiq" replay S010101 2>/dev/null || rc=$?; [ $rc -eq 1 ]  # no such store
"$LOBIQ_BIN/gen_day" 1 120 2>/dev/null | "$LOBIQ_BIN/ingest" "$LOBIQ_DATA/store-S010125-v50" >/dev/null 2>&1
"$lobiq" days | grep "S010125    2025-01-01" >/dev/null
"$lobiq" replay 2025-01-01 | grep "symbols, .* 0 check failures" >/dev/null
"$lobiq" backtest S010125 QRSIM naive >/dev/null 2>&1
ls "$LOBIQ_DATA"/reports/backtest-*/report.html >/dev/null
"$lobiq" strategies | grep avellaneda_stoikov >/dev/null
"$lobiq" bestex 2025-01-01 | grep '^## By symbol' >/dev/null
"$lobiq" surveil 2025-01-01 | grep '^## Summary' >/dev/null
"$lobiq" book 2025-01-01 QRSIM 10:00:00 3 | grep '^QRSIM at 10:00:00' >/dev/null
"$lobiq" leadlag 2025-01-01 QRSIM:QRSIM | grep '^| QRSIM -> QRSIM | 1 |' >/dev/null
"$lobiq" markov 2025-01-01 QRSIM | grep '^## Prediction, out of sample' >/dev/null
"$lobiq" markov 2025-01-01 QRSIM --json | grep '"test":{"moves":' >/dev/null
"$lobiq" mlofi 2025-01-01 QRSIM --interval 60 --split 10:30 | grep '^## Price impact, out of sample' >/dev/null
"$lobiq" lobster 2025-01-01 QRSIM "$LOBIQ_DATA/lob" --levels 3 | grep '^QRSIM' >/dev/null
ls "$LOBIQ_DATA"/lob/QRSIM_2025-01-01_34200000_57600000_orderbook_3.csv >/dev/null
"$lobiq" realism 2025-01-01 QRSIM 2025-01-01 QRSIM | grep '^| \*\*mean\*\* | | | \*\*0.000\*\*' >/dev/null
"$lobiq" realism 2025-01-01 QRSIM | grep '^## Stylised facts' >/dev/null
"$lobiq" vol 2025-01-01 2025-01-01 QRSIM | grep '^## Keys for avellaneda_stoikov' >/dev/null
"$lobiq" options price call 100 100 1 0.03 0 0.2 | grep '^| Black-Scholes' >/dev/null
echo "lobiq cli ok"
