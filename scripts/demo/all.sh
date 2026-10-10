#!/usr/bin/env bash
# The demos that need no downloaded data: a synthetic Nasdaq session through two strategies, and
# paper execution on the bundled recording. Reports go to OUT (default out/). Usage: all.sh [out]
set -u
cd "$(dirname "$0")/../.."
out=${1:-out}
fail=0
echo "== backtest on a synthetic Nasdaq session"
scripts/demo/backtest.sh "$out/backtest" || fail=1
echo "== paper execution on the bundled 20-minute recording"
scripts/demo/paper.sh "$out/paper" || fail=1
echo "reports: $out/backtest/*/report.html, $out/paper/report.html"
exit $fail
