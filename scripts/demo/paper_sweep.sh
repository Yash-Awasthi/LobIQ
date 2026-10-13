#!/usr/bin/env bash
# Demo (g): the maker's gamma and k swept on the bundled recording (9 replays in parallel), ranked report.
# Checks the default setting's hash and the best PnL. Usage: paper_sweep.sh [report-dir]
set -euo pipefail
cd "$(dirname "$0")/../.."
B=${HFT_BIN:-build/native/apps}
[ -x "$B/pm_exec" ] || B=build/release/apps
out=$("$B/pm_exec" --replay data/sample/pm-20261009T1600-20min --config config/exec.cfg --set arb=1 \
      --sweep gamma=0.05,0.1,0.2 --sweep k=10,30,100 --report "${1:-out/paper-sweep}" 2>/dev/null)
default=$(echo "$out" | awk '$1 == "0.1" && $2 == "30" {print $NF}')
best=$(echo "$out" | awk 'NR > 1 && ($3 > m || NR == 2) {m = $3} END {print m}')
echo "sweep: 9 runs, default (gamma 0.1, k 30) decision_hash_v2 $default, best ledger_pnl_usd $best"
[ "$default" = 510bf844d3995b0b ] && [ "$best" = 1.795020 ] || { echo "  expected 510bf844d3995b0b and 1.795020"; exit 1; }
