// Joins the best bid and ask like `imbalance`, and also steps back from the side a related
// leading product points against (docs/results/lead-lag.md): run with --index LEADER, which puts the
// leader's 0.1 s momentum (mid against its EMA, bp) after the symbol's own features.

#include "backtest/registry.hpp"
#include "strategy/features.hpp"

using namespace hft::backtest;

namespace {

struct LeaderJoin {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double threshold = 0;    // |top-of-book imbalance| above this pulls the threatened side
    double lead_bp = 0.5;    // |leader momentum| above this pulls the threatened side
    double sign = 1;         // -1 for an inverse product (SQQQ against QQQ)

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px) return;
        const double imb = (static_cast<double>(b.bid_qty) - static_cast<double>(b.ask_qty)) /
                           static_cast<double>(b.bid_qty + b.ask_qty);
        constexpr std::size_t kLead = hft::strategy::SymbolFeatures::kCount;  // first index, 0.1 s
        const double lead = v.n_features > kLead && std::isfinite(v.features[kLead]) ? sign * v.features[kLead] : 0;
        if (v.inventory < max_inventory && imb > -threshold && lead > -lead_bp) d.bid_px = b.bid_px, d.bid_qty = size;
        if (v.inventory > -max_inventory && imb < threshold && lead < lead_bp) d.ask_px = b.ask_px, d.ask_qty = size;
    }
};

HFT_STRATEGY("leader", "imbalance join that also steps back on a leading product's move (--index LEADER); "
             "keys: size max_inventory threshold lead_bp sign",
             [](const Params& p, const RunSpec&) {
                 LeaderJoin s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.threshold = p.get("threshold", 0);
                 s.lead_bp = p.get("lead_bp", 0.5);
                 s.sign = p.get("sign", 1);
                 return s;
             });

}  // namespace
