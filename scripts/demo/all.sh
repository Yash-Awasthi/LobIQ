#!/usr/bin/env bash
# The demos that need no downloaded data: a synthetic Nasdaq session through two strategies, and
# paper execution on the bundled recording. Reports and an index.html go to OUT (default out/).
set -u
cd "$(dirname "$0")/../.."
out=${1:-out}
fail=0
echo "== backtest on a synthetic Nasdaq session"
scripts/demo/backtest.sh "$out/backtest" || fail=1
echo "== paper execution on the bundled 20-minute recording"
scripts/demo/paper.sh "$out/paper" || fail=1
sed "s/@COMMIT@/${HFT_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}/" scripts/demo/index.html > "$out/index.html"
echo "reports: $out/index.html"
exit $fail
