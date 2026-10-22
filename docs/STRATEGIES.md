# Writing a strategy

Two plug-in points, both "write a type, register it with one line, run it by name":

- [Nasdaq backtester](#nasdaq-backtester): a strategy decides quotes and takes on an order-by-order book.
- [Prediction-market maker](#prediction-market-quoter): a quoter picks bid and ask prices per token for
  `pm_exec` and `pm_live`, live on paper or replayed from a recording.

Start from a template: `scripts/new_strategy.sh my_idea` writes `strategies/my_idea.cpp`, and
`scripts/new_strategy.sh --quoter my_idea` writes `strategies/pm/my_idea.cpp`; both build and run
as written (CI checks that), and print the command to try them. No toolchain? Open the repository
in a dev container (`.devcontainer/`, also GitHub Codespaces): it builds everything and runs the
demos on creation.

## Nasdaq backtester

A strategy for the Nasdaq backtester is a C++ type with one method. Put it in a new file under
`strategies/`, register it with one line, rebuild, and run it by name. The engine does the rest:
market-data and order-entry latency, your orders waiting in the real queues, fees, risk limits,
the end-of-day flatten, and the PnL accounting.

### The whole interface

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

### A complete example

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

### Run it

```
cmake --build build/release                      # a new file needs a reconfigure: cmake --preset release
itch_backtest <store> INTC imbalance threshold=0.5 maker_rebate=2000 taker_fee=3000 --report out/
```

`itch_backtest` with no arguments lists every registered strategy and its keys. `--report DIR`
writes `report.html` (PnL, inventory and fills as charts) plus `fills.csv`, `pnl_minutes.csv`
and `summary.csv`. No data yet? `scripts/demo/backtest.sh` builds a synthetic store and runs one:

```
gen_day 7 390 | ingest /tmp/qrsim                # a synthetic 6.5-hour session, symbol QRSIM
itch_backtest /tmp/qrsim QRSIM imbalance --report out/imbalance
```

Tuning: `--sweep key=v1,v2,...` (repeatable) runs every combination of the listed values on a
thread pool (`THREADS=N`, default all cores) and prints one row per run, ending with
`sharpe_annual`, `psr` and `dsr`; with `--report DIR` it writes `DIR/index.html`, the runs ranked by
total PnL with their curves overlaid and a link to each run's own report. Any key works, strategy or
venue:

```
itch_backtest /tmp/qrsim QRSIM imbalance --sweep threshold=0.2,0.4,0.6,0.8,1 --sweep size=100,200 --report out/sweep
```

Ten runs of a full synthetic day take about 0.4 s on 20 threads. Pick parameters on some days and
check them on others: the best row of one day is partly luck. `dsr` says how much: it is the
probability that a run's Sharpe beats the best one ten random settings would give (each symbol's
runs count as one set of trials; README "Evaluating a strategy", docs/METHODS.md).
`lobiq studio` ranks every report on disk by these numbers and overlays any runs' PnL curves. `scripts/maker_walkforward.sh` and
`scripts/leader_walkforward.sh` do exactly that and print paired bootstrap intervals, since a
maker's total moves with its inventory as much as with its edge.

Cross-asset inputs: `--index SYMBOL[,SYMBOL...]` replays those symbols alongside and appends, for
each, its momentum (mid against its EMA over 0.1 s and 1 s, in bp) to `features`, after the
symbol's own features (`hft::strategy::SymbolFeatures::kCount` of them). `strategies/leader.cpp`
uses the first one to step back when a leading product moves against a quote:

```
itch_backtest <store> TQQQ leader --index QQQ lead_bp=0
itch_backtest <store> SQQQ leader --index QQQ sign=-1     # inverse product
```

Several symbols: give a comma list (`INTC,AAPL,MSFT,AMD`). Each symbol runs on its own, in
parallel, combined with any `--sweep`; the report adds the total over symbols for each parameter
set. On 2025-12-10 (the demo venue settings: 0.2c rebate, 0.3c fee, 50 and 20 us latency),
`naive` with `size=100` makes $1,799 on AMD and loses $4,799 on MSFT: one
symbol says little. Four symbols times two sizes take 2.4 s.

A second example worth reading is `strategies/microprice.cpp`: it quotes around Stoikov's
micro-price, shifted against inventory, and only ever joins or steps back from the best. Its test
shows a useful pattern: with `edge_ticks=0 skew_ticks=0` it must reproduce `naive` fill for fill,
which pins the new code to a known strategy before its own parameters change anything.

```
itch_backtest /tmp/qrsim QRSIM microprice --sweep edge_ticks=0,0.5,1 --sweep skew_ticks=0,0.5,1 --report out/mp
```

### Reinforcement learning

`rl/lobiq_env.py` wraps the backtester as a Gymnasium-style environment (`reset()`, `step(action)`),
standard library only. The `agent` strategy reports the book, inventory, PnL and features every
`step_ms` of market time and quotes what the agent sends back, so an RL loop in Python trades the real
queues with the same latency, fees, risk limits and accounting as any strategy here:

```
from lobiq_env import LobiqEnv
env = LobiqEnv("~/data/store-S121025-v50", "INTC", step_ms=1000, params=["maker_rebate=2000", "taker_fee=3000"])
obs, info = env.reset()
obs, reward, terminated, truncated, info = env.step((0, 0, 100))   # join both best quotes, 100 shares
```

The action is `(bid_offset, ask_offset, size)` in ticks behind the best (`-1` leaves a side empty) or
`None`; the reward is the change in total PnL. `RL.AgentThatJoinsIsNaive` checks that an agent that
always joins ends a day with exactly `naive`'s total.

### What the simulation does and does not model

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

### Checking a strategy

- `zero` must give exactly 0; `random_taker` must lose about half the spread plus fees per share;
  `perfect_foresight` takes on the future mid and must make money. A result far beyond what the
  same idea earns on real data usually means the strategy found a hole in the simulation.
- Synthetic data (`gen_day`) has no informed traders, so plain quoting wins there: `naive` makes
  +$5,611 on the synthetic day and loses $851 on a real INTC day (2025-12-10). Judge strategies on
  real days.
- Add a test next to `tests/backtest_test.cpp` (`Registry.ARegisteredStrategyRunsLikeTheTypeItself`
  shows how to run a strategy by name on the committed fixture).

## Prediction-market quoter

The prediction-market maker quotes every outcome token of the subscribed events. What it quotes is
a quoter: one object per token, asked for a bid and an ask whenever the token's book changes (at
most every `requote_s`, after a `warmup_s`). Put it under `strategies/pm/`.

```cpp
#include "pm/quoter.hpp"

struct Skew final : hft::pm::Quoter {
    hft::pm::Quote quote(const hft::pm::QuoteInput& in) override {
        const double mid = in.mid * 1e4;                                         // price units, 1e-4
        const double shift = std::round(in.inventory / in.params.size) * in.tick;  // lean against inventory
        return {int(mid - 2 * in.tick - shift), int(mid + 2 * in.tick - shift)};
    }
};

HFT_QUOTER("skew", "example: mid +/- 2 ticks, shifted one tick per quote size of inventory",
           [] { return new Skew; });
```

`QuoteInput` holds the token's level-2 `book`, `mid` (a probability), signed `inventory` in
shares, `variance` (EWMA of squared logit-mid changes per second), the maker `params` (`size`,
`max_inv`, `gamma`, `k`, `horizon_s`, ...), the price `tick` and `elapsed_s`. Return `-1` for a
side you do not want to quote.

The maker keeps the safety rules whatever the quoter returns: prices go onto the tick grid (bids
down, asks up), never cross the opposite best (orders are post-only), stay inside (0, 1), and the
side that would push inventory past `max_inv` is not quoted. In `pm_exec`, every order then goes
through pre-trade risk, the order manager and the simulated venue like any other.

```
pm_exec --replay data/sample/pm-20261009T1600-20min --config config/exec.cfg --set arb=1 --quoter skew --report out/skew
pm_exec --paper --run runs/skew --config config/exec.cfg --quoter skew       # live feed, paper only
```

`pm_exec --sweep key=v1,v2,...` (repeatable; any `exec.cfg` key such as `gamma`, `k`, `size`,
`lat_ms`) replays every combination in parallel and ranks them; with `--report DIR` each run gets
its report and `DIR/index.html` links them. A sweep row is the same run as `--set` with those
values, decision hash included. `scripts/demo/paper_sweep.sh` sweeps `gamma` and `k` on the sample.

A paper run stores the quoter's name in `quoter.txt`, so `pm_exec --replay runs/skew` replays it as
it ran. `logit_as` is the built-in model written as a plug-in; it gives the same decision hash as
running without `--quoter` (test `PmQuoter.LogitAsPluginMatchesTheBuiltIn`), which is the check
that the plug-in point changes nothing by itself. On the bundled sample, `skew` fills 17 times and
ends at -$2.16; the built-in fills 5 times and ends at +$1.39.

Fills on this venue come from recorded trades: a resting order fills when a trade prints through
its price, or at its price once the displayed size that was ahead of it has traded. Level-2 data
has no order ids, so that queue position is an estimate, and other traders never react to our
quotes.
