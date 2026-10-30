#!/usr/bin/env bash
# Options tool on a chain priced from a known SVI surface (tests/data/options_chain.csv: S = 100,
# r = 4%, q = 1%; ATM vol 0.2040 at T = 0.25): the fit must recover it with no calendar arbitrage.
set -euo pipefail
bin=$1/options
out=$("$bin" surface "$2" --rate 0.04 --greeks)
echo "$out" | awk -F'|' '$2 + 0 == 0.25 {v=$5; found=1; exit} END{exit !(found && v > 0.199 && v < 0.209)}'
echo "$out" | grep -q 'at 0 of 303 grid points'
p=$("$bin" price put 100 105 1 0.05 0 0.25)
eu=$(echo "$p" | awk -F'|' '/Binomial, European/{print $3}')
am=$(echo "$p" | awk -F'|' '/Binomial, American/{print $3}')
bs=$(echo "$p" | awk -F'|' '/^\| Black-Scholes/{print $3}')
awk -v e="$eu" -v a="$am" -v b="$bs" 'BEGIN{d=e-b; exit !(a > e && d < 0.01 && d > -0.01)}'
rc=0; "$bin" price put 100 -1 1 0.05 0 0.25 2>/dev/null || rc=$?; [ $rc -eq 2 ]
echo "options ok"
