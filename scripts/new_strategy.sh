#!/usr/bin/env bash
# Writes a starting strategy: strategies/NAME.cpp for the Nasdaq backtester, or with --quoter
# strategies/pm/NAME.cpp for the prediction-market maker. Usage: new_strategy.sh [--quoter] NAME
set -euo pipefail
cd "$(dirname "$0")/.."
kind=strategy
[ "${1:-}" = --quoter ] && kind=quoter && shift
name=${1:-}
[[ "$name" =~ ^[a-z][a-z0-9_]*$ ]] || { echo "usage: new_strategy.sh [--quoter] NAME (lowercase, digits, _)" >&2; exit 2; }
type=$(echo "$name" | sed -E 's/(^|_)([a-z])/\U\2/g')
if [ $kind = strategy ]; then
  f=strategies/$name.cpp
  [ -e "$f" ] && { echo "$f exists" >&2; exit 1; }
  cat > "$f" <<EOF
// $name: describe the idea in a line or two (docs/STRATEGIES.md).

#include "backtest/registry.hpp"

using namespace hft::backtest;

namespace {

struct $type {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px) return;
        if (v.inventory < max_inventory) d.bid_px = b.bid_px, d.bid_qty = size;
        if (v.inventory > -max_inventory) d.ask_px = b.ask_px, d.ask_qty = size;
    }
};

HFT_STRATEGY("$name", "keys: size max_inventory", [](const Params& p, const RunSpec&) {
    $type s;
    s.size = static_cast<std::uint32_t>(p.get("size", 100));
    s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
    return s;
});

}  // namespace
EOF
  run="itch_backtest <store> <SYMBOL> $name --report out/$name"
else
  f=strategies/pm/$name.cpp
  [ -e "$f" ] && { echo "$f exists" >&2; exit 1; }
  cat > "$f" <<EOF
// $name: describe the idea in a line or two (docs/STRATEGIES.md).

#include "pm/quoter.hpp"

namespace {

struct $type final : hft::pm::Quoter {
    hft::pm::Quote quote(const hft::pm::QuoteInput& in) override {
        return {in.book.best_bid(), in.book.best_ask()};  // join the best on both sides
    }
};

HFT_QUOTER("$name", "describe it", [] { return new $type; });

}  // namespace
EOF
  run="pm_exec --replay data/sample/pm-20261009T1600-20min --config config/exec.cfg --quoter $name --report out/$name"
fi
echo "wrote $f"
echo "next: cmake --preset release && cmake --build build/release, then"
echo "      $run"
