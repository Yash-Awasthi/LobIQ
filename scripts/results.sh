#!/usr/bin/env bash
# Regenerates every data-derived results page in docs/results from the stored days; the outputs are
# deterministic, so `git diff docs/results` afterwards should be empty unless code or data changed.
# Timing pages (max-performance.md, ms2-book-study.md) are left alone: they depend on the machine.
# Needs the stores of scripts/study.sh (and S101819 for the 2019 pages). Takes about an hour.
set -euo pipefail
cd "$(dirname "$0")/.."
r=docs/results
scripts/study.sh > "$r/microstructure.md"
TEST=S101819 scripts/study.sh > "$r/microstructure-2019.md"
scripts/leadlag.sh > "$r/lead-lag.md"
scripts/maker_walkforward.sh > "$r/maker-walkforward.md"
TEST=S101819 SYMBOLS=SPY,QQQ,IWM,SOXL,SQQQ,TQQQ,TNA,INTC,NVDA scripts/maker_walkforward.sh > "$r/maker-walkforward-2019.md"
scripts/leader_walkforward.sh > "$r/leader-walkforward.md"
scripts/surveillance.sh > "$r/surveillance.md"
"${HFT_BUILD:-build/native}/apps/itch_bestex" "${HFT_DATA:-$HOME/data}/store-S121025-v50" > "$r/execution-quality.md"
git diff --stat -- "$r"
