#!/usr/bin/env bash
# The walk-forward research tools and the new strategies end to end on two synthetic days: volatility
# forecasts, multivariate Hawkes, regimes, the alpha model and its strategy, the conformal maker, and
# a GARCH-driven sweep with its overfitting checks.
set -euo pipefail
apps=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for s in 1 2; do "$apps/gen_day" $s 390 | "$apps/ingest" "$tmp/day$s" >/dev/null 2>&1; done
export THREADS=2

vol=$("$apps/itch_vol" --train "$tmp/day1" --test "$tmp/day2" --symbols QRSIM)
echo "$vol" | awk -F'|' '/^\| QRSIM \|/{a=$3; b=$4; found=1} END{exit !(found && a >= 0 && b >= 0 && a + b < 1)}'
keys=$(echo "$vol" | sed -n 's/^    QRSIM: //p')
[ -n "$keys" ]

"$apps/itch_hawkes" --train "$tmp/day1" --test "$tmp/day2" --symbols QRSIM --window 5 |
    awk -F'|' '/^\| QRSIM \|/{n=$5; found=1} END{exit !(found && n >= 0 && n < 1)}'

"$apps/itch_backtest" "$tmp/day2" QRSIM naive --minutes "$tmp/naive.tsv" >/dev/null
reg=$("$apps/itch_regime" --train "$tmp/day1" --test "$tmp/day2" --symbols QRSIM --states 2 --out "$tmp/reg.csv" --pnl "QRSIM:$tmp/naive.tsv")
echo "$reg" | grep -q '^## Backtest PnL by regime'
[ "$(wc -l <"$tmp/reg.csv")" -eq 381 ]  # header and 380 minutes

alpha=$("$apps/itch_alpha" --train "$tmp/day1" --test "$tmp/day2" --symbols QRSIM --horizon 5 --step 1 --model "$tmp/models" 2>/dev/null)
echo "$alpha" | awk -F'|' '/^\| QRSIM \|/{c=$10; found=1} END{exit !(found && c > 0.8 && c < 0.97)}'  # 90% target coverage
"$apps/itch_backtest" "$tmp/day2" QRSIM "alpha:$tmp/models/QRSIM.alpha" | grep -q '^total_usd'
"$apps/itch_backtest" "$tmp/day2" QRSIM conformal | awk '$1 == "fills" {exit !($2 > 0)}'

# shellcheck disable=SC2086  # keys are separate words
"$apps/itch_backtest" "$tmp/day2" QRSIM avellaneda_stoikov $keys --sweep gamma=0.05,0.1,0.2 --report "$tmp/sweep" 2>"$tmp/err" |
    head -1 | grep -q 'mcs_p$'
grep -q 'QRSIM: 3 runs, SPA p' "$tmp/err"
grep -q 'Overfitting checks' "$tmp/sweep/index.html"
echo "research tools ok"
