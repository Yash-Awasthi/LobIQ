// One strategy on one symbol-day through the event-driven backtest; money printed in dollars,
// accounted in integer micro-dollars. Strategies come from strategies/*.cpp (docs/STRATEGIES.md).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
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
    if (std::ifstream tsv(store / "symbols.tsv"); tsv) {  // written by ingest: "<locate>\t<symbol>"
        std::string loc, name;
        while (tsv >> loc >> name)
            if (name == sym) return static_cast<std::uint16_t>(std::stoul(loc));
    }
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

// Venue, latency and risk settings from the key=value arguments.
void configure(backtest::RunSpec& r, const backtest::Params& p) {
    backtest::Config& c = r.config;
    c = backtest::Config{};
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
}

// --sweep key=a,b,c, repeated: every combination of the listed values, one run each on a thread pool.
int sweep(const backtest::StrategyEntry& entry, const backtest::RunSpec& base, const backtest::Params& p,
          const std::vector<std::pair<std::string, std::vector<double>>>& axes,
          const std::vector<std::pair<std::string, std::uint16_t>>& symbols, const std::string& strategy,
          const std::string& report_dir) {
    std::vector<backtest::SweepRun> runs;
    for (const auto& [name, locate] : symbols) {
        backtest::SweepRun x;
        x.locate = locate;
        if (symbols.size() > 1) x.symbol = name;
        runs.push_back(std::move(x));
    }
    for (const auto& [key, values] : axes) {
        std::vector<backtest::SweepRun> next;
        for (const backtest::SweepRun& run : runs)
            for (const double v : values) {
                backtest::SweepRun x = run;
                x.params.emplace_back(key, v);
                next.push_back(std::move(x));
            }
        runs = std::move(next);
    }
    if (runs.size() > 1024) throw std::runtime_error("sweep has " + std::to_string(runs.size()) + " runs; at most 1024");
    std::atomic<std::size_t> next{0};
    std::string error;
    std::mutex error_lock;
    auto worker = [&] {
        for (std::size_t i; (i = next++) < runs.size();) {
            backtest::Params q = p;
            for (const auto& [k, v] : runs[i].params) q.kv[k] = v;
            backtest::RunSpec r = base;
            r.target = runs[i].locate;
            configure(r, q);
            try {
                runs[i].summary = entry.run(r, q);
                if (!report_dir.empty()) backtest::write_report(report_dir + "/" + runs[i].label(), runs[i].symbol.empty() ? symbols[0].first : runs[i].symbol,
                                                          strategy, r.config, runs[i].summary);
            } catch (const std::exception& e) {
                const std::lock_guard g(error_lock);
                error = e.what();
            }
        }
    };
    const char* env = std::getenv("THREADS");
    const unsigned n = std::max(1u, std::min<unsigned>(env ? static_cast<unsigned>(std::atoi(env)) : std::thread::hardware_concurrency(),
                                                        static_cast<unsigned>(runs.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < n; ++t) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();
    if (!error.empty()) throw std::runtime_error(error);
    if (symbols.size() > 1) std::printf("symbol\t");
    for (const auto& [k, v] : axes) std::printf("%s\t", k.c_str());
    std::printf("total_usd\tspread_usd\tinventory_usd\tfees_usd\tfills\tmax_abs_inventory\tdiverted_shares\n");
    for (const backtest::SweepRun& run : runs) {
        if (symbols.size() > 1) std::printf("%s\t", run.symbol.c_str());
        for (const auto& [k, v] : run.params) std::printf("%g\t", v);
        const backtest::Summary& s = run.summary;
        std::printf("%.2f\t%.2f\t%.2f\t%.2f\t%llu\t%lld\t%llu\n", backtest::usd(s.total), backtest::usd(s.spread),
                    backtest::usd(s.inventory_pnl), backtest::usd(s.fees), (unsigned long long)s.fills,
                    (long long)s.max_abs_inventory, (unsigned long long)s.divergence.diverted);
    }
    if (!report_dir.empty()) {
        std::string names;
        for (const auto& [name, loc] : symbols) names += (names.empty() ? "" : ",") + name;
        backtest::write_sweep_report(report_dir, names, strategy, runs);
        std::fprintf(stderr, "report: %s/index.html\n", report_dir.c_str());
    }
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "usage: itch_backtest <store-dir> <symbol|locate>[,...] <strategy> [key=value ...] [--report DIR]\n"
                 "                     [--sweep key=v1,v2,... (repeatable; every combination, THREADS=N)]\n"
                 "                     [--fills FILE] [--minutes FILE] [--index SYMBOL|LOC,...]\n"
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
    std::vector<std::pair<std::string, std::vector<double>>> axes;
    backtest::RunSpec r;
    r.store = argv[1];
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--fills" || a == "--minutes" || a == "--index" || a == "--report" || a == "--sweep") && i + 1 < argc) {
            const std::string v = argv[++i];
            if (a == "--sweep") {
                const std::size_t eq = v.find('=');
                if (eq == std::string::npos || eq + 1 == v.size()) return usage();
                std::vector<double> values;
                for (std::size_t at = eq + 1; at <= v.size();) {
                    const std::size_t comma = std::min(v.find(',', at), v.size());
                    values.push_back(std::atof(v.substr(at, comma - at).c_str()));
                    at = comma + 1;
                }
                axes.emplace_back(v.substr(0, eq), std::move(values));
            } else if (a == "--fills") fills_path = v;
            else if (a == "--minutes") minutes_path = v;
            else if (a == "--report") report_dir = v;
            else
                for (std::size_t at = 0; at < v.size();) {
                    const std::size_t comma = std::min(v.find(',', at), v.size());
                    r.index.push_back(find_locate(r.store, v.substr(at, comma - at)));
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

    configure(r, p);

    try {
        std::vector<std::pair<std::string, std::uint16_t>> symbols;
        for (const std::string all = argv[2]; const auto part : std::views::split(all, ',')) {
            const std::string name(part.begin(), part.end());
            symbols.emplace_back(name, find_locate(r.store, name));
        }
        r.target = symbols[0].second;
        if (!axes.empty() || symbols.size() > 1) return sweep(*entry, r, p, axes, symbols, strategy, report_dir);
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
