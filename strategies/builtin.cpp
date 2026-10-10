// The reference strategies of src/backtest/strategies.hpp, registered by name.

#include "backtest/registry.hpp"
#include "backtest/strategies.hpp"
#include "book/itch_apply.hpp"

using namespace hft;
using namespace hft::backtest;

namespace {

std::uint32_t size_of(const Params& p) { return static_cast<std::uint32_t>(p.get("size", 100)); }
std::int64_t max_inv(const Params& p) { return static_cast<std::int64_t>(p.get("max_inventory", 500)); }

HFT_STRATEGY("zero", "never trades (PnL must be exactly zero)", [](const Params&, const RunSpec&) { return Zero{}; });

HFT_STRATEGY("naive", "joins the best bid and ask; keys: size max_inventory",
             [](const Params& p, const RunSpec&) { return NaiveJoin{size_of(p), max_inv(p)}; });

HFT_STRATEGY("random_taker", "takes on a random side; keys: rate size", [](const Params& p, const RunSpec&) {
    RandomTaker s;
    s.rate = p.get("rate", 0.001);
    s.size = size_of(p);
    return s;
});

HFT_STRATEGY("random_passive", "quotes at random distances; keys: size", [](const Params& p, const RunSpec&) {
    RandomPassive s;
    s.size = size_of(p);
    return s;
});

HFT_STRATEGY("perfect_foresight", "sees the future mid (upper bound); keys: horizon_ns cost_ticks size",
             [](const Params& p, const RunSpec& r) {
                 PerfectForesight s;
                 // The mid series is read ahead: this strategy alone may see the future.
                 book::TickBook b(4096);
                 book::ItchApply<book::TickBook> ap{b};
                 data::SymbolReader rd(r.store, r.target);
                 data::Record rec{};
                 while (rd.next(rec)) {
                     itch::dispatch(rec.data, rec.len, ap);
                     const book::Bbo q = b.bbo();
                     if (!q.bid_px || !q.ask_px || q.ask_px <= q.bid_px) continue;
                     s.ts.push_back(itch::detail::read_header(rec.data).timestamp);
                     s.mid.push_back((static_cast<double>(q.bid_px) + q.ask_px) / (2.0 * r.config.exchange.tick));
                 }
                 s.horizon_ns = static_cast<std::uint64_t>(p.get("horizon_ns", 1e9));
                 s.cost_ticks = p.get("cost_ticks", 0.3);
                 s.size = size_of(p);
                 return s;
             });

HFT_STRATEGY("avellaneda_stoikov",
             "inventory-skewed market maker; keys: gamma A k glft online max_dist_ticks online_tau_s online_prior_s "
             "size max_inventory",
             [](const Params& p, const RunSpec& r) {
                 AvellanedaStoikov s;
                 s.gamma = p.get("gamma", 0.1);
                 s.A = p.get("A", 1.0);
                 s.k = p.get("k", 1.5);
                 s.glft = p.get("glft", 1) != 0;
                 s.online = p.get("online", 0) != 0;
                 s.max_dist_ticks = p.get("max_dist_ticks", 0);
                 s.online_tau_s = p.get("online_tau_s", 600);
                 s.online_prior_s = p.get("online_prior_s", 60);
                 s.size = size_of(p);
                 s.max_inventory = max_inv(p);
                 s.end_ns = r.config.end_ns;
                 s.tick = r.config.exchange.tick;
                 return s;
             });

HFT_STRATEGY("dp:", "offline-solved policy table: dp:<policy.bin>", [](const Params& p, const RunSpec& r) {
    DpPolicy s;
    s.load(p.arg);
    s.tick = r.config.exchange.tick;
    return s;
});

HFT_STRATEGY("ext:", "policy table with guards: ext:<policy.bin>; keys: toxicity taking vol_limit take_margin",
             [](const Params& p, const RunSpec& r) {
                 Extended s;
                 s.dp.load(p.arg);
                 s.dp.tick = r.config.exchange.tick;
                 s.toxicity = p.get("toxicity", 1) != 0;
                 s.taking = p.get("taking", 1) != 0;
                 s.vol_limit = p.get("vol_limit", 1.0);
                 s.fee_ticks = static_cast<double>(r.config.exchange.taker_fee) / (r.config.exchange.tick * 100.0);
                 s.take_margin = p.get("take_margin", 0.1);
                 return s;
             });

}  // namespace
