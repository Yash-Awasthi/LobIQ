#!/usr/bin/env bash
# Demo (f): paper execution on the bundled 20-minute recording, makers and the arbitrage executor on,
# with an HTML report. Usage: paper.sh [report-dir]
cd "$(dirname "$0")/../.."
exec scripts/demo/run.sh 1a2778b0f923a6d4 "orders maker_fills taker_fills arb_windows arb_attempts ledger_pnl_usd kill" \
  --replay data/sample/pm-20261009T1600-20min --config config/exec.cfg --set arb=1 --report "${1:-out/paper-demo}"
