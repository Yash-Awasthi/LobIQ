// Market maker whose half-spread is an adaptive conformal interval (docs/METHODS.md "Conformal
// intervals"): the micro-price forecasts the mid horizon_s ahead, and each quote sits where the mid
// lands outside the forecast's (1 - alpha) interval, so the spread widens and narrows with how wrong
// recent forecasts were, keeping the miss rate near alpha whatever the volatility regime.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "backtest/registry.hpp"
#include "strategy/conformal.hpp"

using namespace lobiq::backtest;

namespace {

struct ConformalMaker {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double tick = 100;
    double horizon_s = 1, sample_s = 0.1;
    double min_edge_ticks = 0.5, skew_ticks = 0.5;
    std::size_t warmup = 100;
    lobiq::strategy::conformal::Adaptive aci;

    struct Pending {
        std::uint64_t ts;
        double mid, pred, width;
    };
    // At most horizon / sample + 1 forecasts are open at once: a ring sized for that never allocates.
    std::vector<Pending> ring;
    std::size_t head = 0, open = 0;
    std::uint64_t next_sample = 0, horizon_ns = 1'000'000'000, sample_ns = 100'000'000;

    void decide(const View& v, Desired& d) {
        const lobiq::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px || !std::isfinite(v.mid)) return;
        const double bq = static_cast<double>(b.bid_qty), aq = static_cast<double>(b.ask_qty);
        const double micro = (static_cast<double>(b.bid_px) * aq + static_cast<double>(b.ask_px) * bq) / (bq + aq) / tick;
        for (; open && v.now >= ring[head].ts + horizon_ns; --open) {
            const Pending& p = ring[head];
            aci.observe(std::abs(v.mid - p.mid - p.pred), p.width);
            head = head + 1 == ring.size() ? 0 : head + 1;
        }
        const double q = aci.quantile();
        if (v.now >= next_sample && open < ring.size()) {
            const std::size_t at = head + open < ring.size() ? head + open : head + open - ring.size();
            ring[at] = {v.now, v.mid, micro - v.mid, q};
            ++open;
            next_sample = v.now + sample_ns;
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

LOBIQ_STRATEGY("conformal",
             "quotes the micro-price +/- an adaptive conformal interval of the mid horizon_s ahead; "
             "keys: size max_inventory alpha gamma window horizon_s sample_s min_edge_ticks skew_ticks warmup",
             [](const Params& p, const RunSpec& r) {
                 ConformalMaker s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.tick = static_cast<double>(r.config.exchange.tick);
                 s.horizon_s = p.get("horizon_s", 1);
                 s.sample_s = p.get("sample_s", 0.1);
                 if (!(s.horizon_s > 0 && s.sample_s > 0)) throw std::runtime_error("conformal needs horizon_s > 0 and sample_s > 0");
                 s.horizon_ns = static_cast<std::uint64_t>(s.horizon_s * 1e9);
                 s.sample_ns = static_cast<std::uint64_t>(s.sample_s * 1e9);
                 s.ring.resize(static_cast<std::size_t>(std::ceil(s.horizon_s / s.sample_s)) + 2);
                 s.min_edge_ticks = p.get("min_edge_ticks", 0.5);
                 s.skew_ticks = p.get("skew_ticks", 0.5);
                 s.warmup = static_cast<std::size_t>(p.get("warmup", 100));
                 s.aci = lobiq::strategy::conformal::Adaptive(p.get("alpha", 0.2), p.get("gamma", 0.005),
                                                            static_cast<std::size_t>(p.get("window", 500)));
                 return s;
             });

}  // namespace
