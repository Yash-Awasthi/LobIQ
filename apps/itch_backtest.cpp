// One strategy on one symbol-day through the event-driven backtest; money printed in dollars,
// accounted in integer micro-dollars. Strategies come from strategies/*.cpp (docs/STRATEGIES.md).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "backtest/registry.hpp"
#include "backtest/report.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"

using namespace hft;

namespace {

struct Name {
    std::string& out;
    void operator()(const itch::StockDirectory& m) {
        out.assign(m.stock, 8);
        out.erase(out.find_last_not_of(' ') + 1);
    }
    template <class T>
    void operator()(const T&) {}
};

// Locate of a ticker: the stock directory message is among the first records of a symbol.
std::uint16_t find_locate(const std::filesystem::path& store, const std::string& sym) {
    if (!sym.empty() && std::all_of(sym.begin(), sym.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return static_cast<std::uint16_t>(std::stoul(sym));
    for (const auto& e : std::filesystem::directory_iterator(store)) {
        if (e.path().extension() != ".idx") continue;
        const auto loc = static_cast<std::uint16_t>(std::stoul(e.path().stem().string()));
        data::SymbolReader rd(store, loc);
        data::Record rec{};
        std::string name;
        Name h{name};
        for (int i = 0; i < 8 && name.empty() && rd.next(rec); ++i)
            if (rec.data[0] == 'R') itch::dispatch(rec.data, rec.len, h);
        if (name == sym) return loc;
    }
    throw std::runtime_error("symbol " + sym + " not in store");
}

int usage() {
    std::fprintf(stderr,
                 "usage: itch_backtest <store-dir> <symbol|locate> <strategy> [key=value ...] [--report DIR]\n"
                 "                     [--fills FILE] [--minutes FILE] [--index LOC,...]\n"
                 "strategies:\n");
    for (const auto& e : backtest::strategies())
        std::fprintf(stderr, "  %-20s %s\n", (e.name + (e.takes_arg ? ":<arg>" : "")).c_str(), e.help.c_str());
    std::fprintf(stderr,
                 "venue keys: tick maker_rebate taker_fee (micro-dollars per share) fill_rule (0 queue, 1 trade-through)\n"
                 "            market_data_ns order_entry_ns processing_ns start_ns stop_ns end_ns sec_fee_per_million\n"
                 "            taf_per_share taf_max max_position max_order collar_ticks hysteresis_ticks\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return usage();
    const std::string strategy = argv[3];
    backtest::Params p;
    std::string fills_path, minutes_path, report_dir;
    backtest::RunSpec r;
    r.store = argv[1];
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--fills" || a == "--minutes" || a == "--index" || a == "--report") && i + 1 < argc) {
            const std::string v = argv[++i];
            if (a == "--fills") fills_path = v;
            else if (a == "--minutes") minutes_path = v;
            else if (a == "--report") report_dir = v;
            else
                for (std::size_t at = 0; at < v.size();) {
                    const std::size_t comma = std::min(v.find(',', at), v.size());
                    r.index.push_back(static_cast<std::uint16_t>(std::stoul(v.substr(at, comma - at))));
                    at = comma + 1;
                }
            continue;
        }
        const std::size_t eq = a.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "expected key=value, got %s\n", a.c_str());
            return usage();
        }
        p.kv[a.substr(0, eq)] = std::atof(a.c_str() + eq + 1);
    }
    const backtest::StrategyEntry* entry = backtest::find_strategy(strategy);
    if (!entry) {
        std::fprintf(stderr, "unknown strategy %s\n", strategy.c_str());
        return usage();
    }
    if (entry->takes_arg) p.arg = strategy.substr(entry->name.size() + 1);

    backtest::Config& c = r.config;
    c.exchange.tick = static_cast<std::uint32_t>(p.get("tick", 100));
    c.exchange.maker_rebate = static_cast<std::int64_t>(p.get("maker_rebate", 0));
    c.exchange.taker_fee = static_cast<std::int64_t>(p.get("taker_fee", 0));
    c.fill_rule = p.get("fill_rule", 0) != 0 ? engine::FillRule::TradeThrough : engine::FillRule::Queue;
    c.market_data_ns = static_cast<std::uint64_t>(p.get("market_data_ns", 0));
    c.order_entry_ns = static_cast<std::uint64_t>(p.get("order_entry_ns", 0));
    c.processing_ns = static_cast<std::uint64_t>(p.get("processing_ns", 0));
    c.start_ns = static_cast<std::uint64_t>(p.get("start_ns", static_cast<double>(c.start_ns)));
    c.stop_ns = static_cast<std::uint64_t>(p.get("stop_ns", static_cast<double>(c.stop_ns)));
    c.end_ns = static_cast<std::uint64_t>(p.get("end_ns", static_cast<double>(c.end_ns)));
    c.sec_fee_per_million = static_cast<std::int64_t>(p.get("sec_fee_per_million", 0));
    c.taf_per_share = static_cast<std::int64_t>(p.get("taf_per_share", 0));
    c.taf_max = static_cast<std::int64_t>(p.get("taf_max", 0));
    c.risk.max_position = static_cast<std::int64_t>(p.get("max_position", 1000));
    c.risk.max_order = static_cast<std::uint32_t>(p.get("max_order", 500));
    c.risk.collar_ticks = static_cast<std::uint32_t>(p.get("collar_ticks", 20));
    r.hysteresis = static_cast<std::uint32_t>(p.get("hysteresis_ticks", 0));

    try {
        r.target = find_locate(r.store, argv[2]);
        const auto t0 = std::chrono::steady_clock::now();
        const backtest::Summary s = entry->run(r, p);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        auto usd = [](std::int64_t micro) { return static_cast<double>(micro) * 1e-6; };
        std::printf("symbol\t%s\nlocate\t%u\nstrategy\t%s\n", argv[2], r.target, strategy.c_str());
        std::printf("total_usd\t%.2f\nspread_usd\t%.2f\ninventory_usd\t%.2f\nfees_usd\t%.2f\nadverse_usd\t%.2f\n",
                    usd(s.total), usd(s.spread), usd(s.inventory_pnl), usd(s.fees), usd(s.adverse));
        std::printf("fills\t%llu\norders\t%llu\ncancels\t%llu\nrejects\t%llu\nvolume\t%lld\n",
                    (unsigned long long)s.fills, (unsigned long long)s.orders, (unsigned long long)s.cancels,
                    (unsigned long long)s.rejects, (long long)s.volume);
        std::printf("end_inventory\t%lld\nmax_abs_inventory\t%lld\nfills_during_cancel\t%llu\n",
                    (long long)s.end_inventory, (long long)s.max_abs_inventory, (unsigned long long)s.fills_during_cancel);
        std::printf("diverted_shares\t%llu\nthrough_fills\t%llu\nhidden_fills\t%llu\nevents\t%llu\nseconds\t%.2f\n",
                    (unsigned long long)s.divergence.diverted, (unsigned long long)s.divergence.through_fills,
                    (unsigned long long)s.divergence.hidden_fills, (unsigned long long)s.events, secs);
        if (!fills_path.empty()) backtest::write_fills(fills_path, s);
        if (!minutes_path.empty()) backtest::write_minutes(minutes_path, s);
        if (!report_dir.empty()) {
            backtest::write_report(report_dir, argv[2], strategy, r.config, s);
            std::fprintf(stderr, "report: %s/report.html\n", report_dir.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
