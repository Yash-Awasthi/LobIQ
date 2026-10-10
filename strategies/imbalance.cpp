// Example strategy (docs/STRATEGIES.md): joins the best bid and ask, but steps back from the side
// the top-of-book imbalance points against, and stops adding to inventory past a cap.

#include "backtest/registry.hpp"

using namespace hft::backtest;

namespace {

struct ImbalanceJoin {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double threshold = 0.6;  // |imbalance| above this pulls the quote on the threatened side

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px) return;
        // +1: all size on the bid (price likely to rise), -1: all on the ask.
        const double imb = (static_cast<double>(b.bid_qty) - static_cast<double>(b.ask_qty)) /
                           static_cast<double>(b.bid_qty + b.ask_qty);
        if (v.inventory < max_inventory && imb > -threshold) d.bid_px = b.bid_px, d.bid_qty = size;
        if (v.inventory > -max_inventory && imb < threshold) d.ask_px = b.ask_px, d.ask_qty = size;
    }
};

HFT_STRATEGY("imbalance", "example: joins the best, steps back on top-of-book imbalance; keys: size max_inventory threshold",
             [](const Params& p, const RunSpec&) {
                 ImbalanceJoin s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.threshold = p.get("threshold", 0.6);
                 return s;
             });

}  // namespace
