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
scripts/demo/paper_sweep.sh "$out/paper-sweep" || fail=1
if command -v python3 >/dev/null; then
  echo "== forecasting models and options"
  scripts/demo/models.sh "$out/models" || fail=1
  echo "== leaderboard snapshot, with every studio tool run on two synthetic days"
  demo=$(mktemp -d)
  B=${LOBIQ_BIN:-build/release/apps}
  for d in "7 S010725" "8 S010825"; do
    set -- $d
    "$B/gen_day" "$1" 390 2>/dev/null | "$B/ingest" "$demo/store-$2-v50" >/dev/null 2>&1 && "$B/checkpoint" "$demo/store-$2-v50" >/dev/null 2>&1
  done
  LOBIQ_DATA="$demo" LOBIQ_REPORTS="$demo/reports" LOBIQ_BIN="$(cd "$B" && pwd)" python3 studio/server.py --snapshot "$out" --demo || fail=1
  rm -rf "$demo"
fi
sed "s/@COMMIT@/${LOBIQ_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}/" scripts/demo/index.html > "$out/index.html"
echo "reports: $out/index.html"
exit $fail
