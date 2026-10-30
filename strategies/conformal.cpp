// Market maker whose half-spread is an adaptive conformal interval (docs/METHODS.md "Conformal
// intervals"): the micro-price forecasts the mid horizon_s ahead, and each quote sits where the mid
// lands outside the forecast's (1 - alpha) interval, so the spread widens and narrows with how wrong
// recent forecasts were, keeping the miss rate near alpha whatever the volatility regime.

#include <algorithm>
#include <cmath>
#include <deque>

#include "backtest/registry.hpp"
#include "strategy/conformal.hpp"

using namespace hft::backtest;

namespace {

struct ConformalMaker {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double tick = 100;
    double horizon_s = 1, sample_s = 0.1;
    double min_edge_ticks = 0.5, skew_ticks = 0.5;
    std::size_t warmup = 100;
    hft::strategy::conformal::Adaptive aci;

    struct Pending {
        std::uint64_t ts;
        double mid, pred, width;
    };
    std::deque<Pending> pending;
    std::uint64_t next_sample = 0;

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px || !std::isfinite(v.mid)) return;
        const double bq = static_cast<double>(b.bid_qty), aq = static_cast<double>(b.ask_qty);
        const double micro = (static_cast<double>(b.bid_px) * aq + static_cast<double>(b.ask_px) * bq) / (bq + aq) / tick;
        const auto h = static_cast<std::uint64_t>(horizon_s * 1e9);
        while (!pending.empty() && v.now >= pending.front().ts + h) {
            const Pending& p = pending.front();
            aci.observe(std::abs(v.mid - p.mid - p.pred), p.width);
            pending.pop_front();
        }
        const double q = aci.quantile();
        if (v.now >= next_sample) {
            pending.push_back({v.now, v.mid, micro - v.mid, q});
            next_sample = v.now + static_cast<std::uint64_t>(sample_s * 1e9);
        }
        if (aci.observed() < warmup || !std::isfinite(q)) return;
        const double half = std::max(q, min_edge_ticks);
        const double center = micro - skew_ticks * static_cast<double>(v.inventory) / size;
        // Post-only: never at or through the opposite best.
        const double bid = std::min(std::floor(center - half + 1e-9) * tick, static_cast<double>(b.ask_px) - tick);
        const double ask = std::max(std::ceil(center + half - 1e-9) * tick, static_cast<double>(b.bid_px) + tick);
        if (v.inventory < max_inventory && bid > 0) d.bid_px = static_cast<std::uint32_t>(bid), d.bid_qty = size;
        if (v.inventory > -max_inventory) d.ask_px = static_cast<std::uint32_t>(ask), d.ask_qty = size;
    }
};

HFT_STRATEGY("conformal",
             "quotes the micro-price +/- an adaptive conformal interval of the mid horizon_s ahead; "
             "keys: size max_inventory alpha gamma window horizon_s sample_s min_edge_ticks skew_ticks warmup",
             [](const Params& p, const RunSpec& r) {
                 ConformalMaker s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.tick = static_cast<double>(r.config.exchange.tick);
                 s.horizon_s = p.get("horizon_s", 1);
                 s.sample_s = p.get("sample_s", 0.1);
                 s.min_edge_ticks = p.get("min_edge_ticks", 0.5);
                 s.skew_ticks = p.get("skew_ticks", 0.5);
                 s.warmup = static_cast<std::size_t>(p.get("warmup", 100));
                 s.aci = hft::strategy::conformal::Adaptive(p.get("alpha", 0.2), p.get("gamma", 0.005),
                                                            static_cast<std::size_t>(p.get("window", 500)));
                 return s;
             });

}  // namespace
