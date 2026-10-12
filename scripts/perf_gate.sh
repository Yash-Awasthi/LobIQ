#!/usr/bin/env bash
# Fails when a change makes a hot path execute more than LIMIT_PCT% more instructions than its base.
# Usage: perf_gate.sh <base-build-dir> <head-build-dir>   (both built with the release preset)
# Instruction counts under cachegrind are deterministic to about 0.002% run to run and do not
# depend on the machine, so shared CI runners can gate on them where timings would be noise.
set -euo pipefail
base=${1:?base build dir}
head=${2:?head build dir}
limit=${LIMIT_PCT:-3}
sample=$(dirname "$0")/../data/sample/pm-20261009T1600-20min
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$head/apps/gen_day" 7 390 2>/dev/null | "$head/apps/ingest" "$tmp/store" >/dev/null

ir() {
    valgrind --tool=cachegrind --cache-sim=no --cachegrind-out-file=/dev/null "$@" 2>&1 >/dev/null |
        grep -oE "I +refs: +[0-9,]+" | tr -dc '0-9'
}
declare -A work=(
    [book]="bench/book_study $tmp/store book:tick 5 1 1"
    [book_replay]="apps/book_replay $tmp/store tick"
    [pm_live_replay]="apps/pm_live --replay $sample"
)
fail=0
{
    echo "| Workload | Base instructions | Head instructions | Change |"
    echo "|---|---|---|---|"
    for w in book book_replay pm_live_replay; do
        # shellcheck disable=SC2086
        b=$(THREADS=1 ir "$base"/${work[$w]})
        # shellcheck disable=SC2086
        h=$(THREADS=1 ir "$head"/${work[$w]})
        pct=$(awk -v b="$b" -v h="$h" 'BEGIN{printf "%+.2f", (h - b) * 100 / b}')
        awk -v p="$pct" -v l="$limit" 'BEGIN{exit !(p > l)}' && fail=1
        echo "| $w | $b | $h | $pct% |"
    done
} > "$tmp/table"
tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}" < "$tmp/table"
[ "$fail" = 0 ] || { echo "instruction count up more than $limit% on a hot path" >&2; exit 1; }
