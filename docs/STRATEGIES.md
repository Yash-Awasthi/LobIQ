# Writing a strategy

A strategy for the Nasdaq backtester is a C++ type with one method. Put it in a new file under
`strategies/`, register it with one line, rebuild, and run it by name. The engine does the rest:
market-data and order-entry latency, your orders waiting in the real queues, fees, risk limits,
the end-of-day flatten, and the PnL accounting.

## The whole interface

```cpp
void decide(const View& v, Desired& d);
```

`decide` runs on every market event the strategy sees, inside the quoting window (`start_ns` to
`stop_ns`); after it the engine flattens your inventory by itself. It reads `View` and fills in
`Desired`; the backtester turns the difference between what you want and what you have into new
orders and cancels. Resting quotes are post-only.

`View` (all read-only, from `src/backtest/backtest.hpp`):

| Field | Meaning |
|---|---|
| `now` | the strategy's clock, ns since midnight (the market as it looked `market_data_ns` ago) |
| `book` | full order-by-order book of the symbol: `book.bbo()` gives best bid and ask with sizes |
| `mid` | mid price in ticks, NaN when one side is empty |
| `inventory` | shares held, signed |
| `working` | your open orders: price, open size, and the shares queued ahead of each |
| `features`, `n_features` | streaming features (order flow imbalance, EMAs, volatility) |

`Desired`:

| Field | Meaning |
|---|---|
| `bid_px`, `bid_qty` | a resting buy you want (qty 0 for none); prices in ITCH units, $0.0001 |
| `ask_px`, `ask_qty` | a resting sell you want |
| `take_side`, `take_qty`, `take_limit` | an immediate-or-cancel order: side +1 buy, -1 sell |

Leaving `Desired` untouched means "no orders": anything resting is cancelled.

## A complete example

`strategies/imbalance.cpp` joins the best bid and ask, but steps back from the side that the
top-of-book size imbalance points against:

```cpp
#include "backtest/registry.hpp"

using namespace hft::backtest;

namespace {

struct ImbalanceJoin {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double threshold = 0.6;

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px) return;
        const double imb = (double(b.bid_qty) - double(b.ask_qty)) / double(b.bid_qty + b.ask_qty);
        if (v.inventory < max_inventory && imb > -threshold) d.bid_px = b.bid_px, d.bid_qty = size;
        if (v.inventory > -max_inventory && imb < threshold) d.ask_px = b.ask_px, d.ask_qty = size;
    }
};

HFT_STRATEGY("imbalance", "example: joins the best, steps back on imbalance; keys: size max_inventory threshold",
             [](const Params& p, const RunSpec&) {
                 ImbalanceJoin s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.threshold = p.get("threshold", 0.6);
                 return s;
             });

}  // namespace
```

The factory gets the command line's `key=value` pairs (`p.get(key, default)`) and the run's
settings (`RunSpec`: config, store, symbol). A name ending in `:` takes an argument, as in
`dp:policy.bin`; the text after the colon is `p.arg`.

## Run it

```
cmake --build build/release                      # a new file needs a reconfigure: cmake --preset release
itch_backtest <store> INTC imbalance threshold=0.5 maker_rebate=2000 taker_fee=3000 --report out/
```

`itch_backtest` with no arguments lists every registered strategy and its keys. `--report DIR`
writes `report.html` (PnL, inventory and fills as charts) plus `fills.csv`, `pnl_minutes.csv`
and `summary.csv`. No data yet? `scripts/demo/backtest.sh` builds a synthetic store and runs one.

## What the simulation does and does not model

- **Queue position:** your order joins the queue behind every real order already resting at its
  price, as a new order would. When a recorded trade would have reached your order first, you are
  filled and the shares are counted in `diverted_shares`.
- **Real orders are never moved.** The recorded market stays as it was; nobody reacts to your
  orders. That is accurate for small orders and optimistic for large ones: a `diverted_shares`
  figure that is a large part of the volume at your prices means the result is not trustworthy.
- **Latency:** you see the market `market_data_ns` late; your orders arrive `order_entry_ns`
  after you send them; reports come back one order-entry latency later.
- **Fills during a cancel** count: a cancel in flight can still be filled.
- **Fees:** `maker_rebate`, `taker_fee` per share, SEC and FINRA fees on sales.
- **Risk:** position and order-size limits and a price collar reject orders that break them.
- **Accounting:** total = spread capture + inventory PnL + fees, checked after every event.

## Checking a strategy

- `zero` must give exactly 0; `random_taker` must lose about half the spread plus fees per share;
  `perfect_foresight` takes on the future mid and must make money. A result far beyond what the
  same idea earns on real data usually means the strategy found a hole in the simulation.
- Synthetic data (`gen_day`) has no informed traders, so plain quoting wins there: `naive` makes
  +$5,611 on the synthetic day and loses $851 on a real INTC day (2025-12-10). Judge strategies on
  real days.
- Add a test next to `tests/backtest_test.cpp` (`Registry.ARegisteredStrategyRunsLikeTheTypeItself`
  shows how to run a strategy by name on the committed fixture).
