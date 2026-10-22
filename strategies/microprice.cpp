// Market maker around Stoikov's micro-price (docs/METHODS.md "Micro-price"): the fair value is the
// size-weighted mid, shifted against inventory; each quote joins the best or steps back from it,
// never improving it. With edge_ticks=0 and skew_ticks=0 it quotes exactly like `naive`.

#include <algorithm>
#include <cmath>

#include "backtest/registry.hpp"

using namespace hft::backtest;

namespace {

struct MicroPriceMaker {
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double tick = 100;        // ITCH price units ($0.0001)
    double edge_ticks = 0.5;  // half-width of the quote around the reservation price
    double skew_ticks = 0.5;  // reservation shift per `size` shares of inventory

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px) return;
        const double bq = static_cast<double>(b.bid_qty), aq = static_cast<double>(b.ask_qty);
        // A large bid queue makes the next move more likely up, so the fair value leans to the ask.
        const double micro = (static_cast<double>(b.bid_px) * aq + static_cast<double>(b.ask_px) * bq) / (bq + aq);
        const double reservation = micro - skew_ticks * tick * static_cast<double>(v.inventory) / size;
        // The epsilon keeps rounding from moving a price that sits on the grid by a whole tick.
        const double bid = std::floor((reservation - edge_ticks * tick) / tick + 1e-9) * tick;
        const double ask = std::ceil((reservation + edge_ticks * tick) / tick - 1e-9) * tick;
        if (v.inventory < max_inventory && bid > 0)
            d.bid_px = std::min(b.bid_px, static_cast<std::uint32_t>(bid)), d.bid_qty = size;
        if (v.inventory > -max_inventory) d.ask_px = std::max(b.ask_px, static_cast<std::uint32_t>(ask)), d.ask_qty = size;
    }
};

HFT_STRATEGY("microprice",
             "quotes around the micro-price, skewed against inventory, never inside the best; "
             "keys: size max_inventory edge_ticks skew_ticks",
             [](const Params& p, const RunSpec& r) {
                 MicroPriceMaker s;
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.tick = static_cast<double>(r.config.exchange.tick);
                 s.edge_ticks = p.get("edge_ticks", 0.5);
                 s.skew_ticks = p.get("skew_ticks", 0.5);
                 return s;
             });

}  // namespace
