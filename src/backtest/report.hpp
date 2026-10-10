#pragma once

// Run outputs of itch_backtest: fills and per-minute PnL as TSV/CSV, and one HTML report.

#include <cstdio>
#include <filesystem>
#include <string>

#include "backtest/backtest.hpp"
#include "report/html.hpp"

namespace hft::backtest {

inline double usd(std::int64_t micro) { return static_cast<double>(micro) * 1e-6; }

inline void write_fills(const std::string& path, const Summary& r, char sep = '\t') {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "ts_ns%csigned_shares%cprice%cmaker\n", sep, sep, sep);
    for (std::size_t i = 0; i < r.fill_ts.size(); ++i)
        std::fprintf(f, "%llu%c%lld%c%u%c%u\n", (unsigned long long)r.fill_ts[i], sep, (long long)r.fill_signed_shares[i], sep,
                     r.fill_price[i], sep, r.fill_maker[i]);
    std::fclose(f);
}

inline void write_minutes(const std::string& path, const Summary& r, char sep = '\t') {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "minute%ctotal_usd\n", sep);
    for (std::size_t i = 0; i < r.minute_pnl.size(); ++i) std::fprintf(f, "%zu%c%.2f\n", i, sep, usd(r.minute_pnl[i]));
    std::fclose(f);
}

// DIR/report.html, DIR/fills.csv, DIR/pnl_minutes.csv, DIR/summary.csv.
inline void write_report(const std::string& dir, const std::string& symbol, const std::string& strategy, const Config& c,
                         const Summary& s) {
    std::filesystem::create_directories(dir);
    write_fills(dir + "/fills.csv", s, ',');
    write_minutes(dir + "/pnl_minutes.csv", s, ',');
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"total_usd", report::num(usd(s.total))},
        {"spread_usd", report::num(usd(s.spread))},
        {"inventory_usd", report::num(usd(s.inventory_pnl))},
        {"fees_usd", report::num(usd(s.fees))},
        {"adverse_usd", report::num(usd(s.adverse))},
        {"fills", std::to_string(s.fills)},
        {"orders", std::to_string(s.orders)},
        {"cancels", std::to_string(s.cancels)},
        {"rejects", std::to_string(s.rejects)},
        {"volume_shares", std::to_string(s.volume)},
        {"end_inventory", std::to_string(s.end_inventory)},
        {"max_abs_inventory", std::to_string(s.max_abs_inventory)},
        {"fills_during_cancel", std::to_string(s.fills_during_cancel)},
        {"diverted_shares", std::to_string(s.divergence.diverted)},
        {"through_fills", std::to_string(s.divergence.through_fills)},
        {"hidden_fills", std::to_string(s.divergence.hidden_fills)},
        {"events", std::to_string(s.events)},
    };
    std::string csv = "key,value\n";
    for (const auto& [k, v] : rows) csv += k + "," + v + "\n";
    report::write_file(dir + "/summary.csv", csv);

    report::Series pnl("total PnL");
    for (std::size_t i = 0; i < s.minute_pnl.size(); ++i) pnl.x.push_back(static_cast<double>(i + 1)), pnl.y.push_back(usd(s.minute_pnl[i]));
    report::Series inv("inventory");
    inv.step = true;
    report::Series buys("buy", "var(--s3)"), sells("sell", "var(--s2)");
    buys.points = sells.points = true;
    double pos = 0;
    for (std::size_t i = 0; i < s.fill_ts.size(); ++i) {
        const double h = static_cast<double>(s.fill_ts[i]) / 3.6e12;
        pos += static_cast<double>(s.fill_signed_shares[i]);
        inv.x.push_back(h), inv.y.push_back(pos);
        (s.fill_signed_shares[i] > 0 ? buys : sells).x.push_back(h);
        (s.fill_signed_shares[i] > 0 ? buys : sells).y.push_back(static_cast<double>(s.fill_price[i]) * 1e-4);
    }
    const auto maker = std::count(s.fill_maker.begin(), s.fill_maker.end(), std::uint8_t{1});
    std::string body = report::cards({{"Total PnL", report::money(usd(s.total))},
                                      {"Spread capture", report::money(usd(s.spread))},
                                      {"Inventory PnL", report::money(usd(s.inventory_pnl))},
                                      {"Fees (+ is net rebate)", report::money(usd(s.fees))},
                                      {"Fills", std::to_string(s.fills) + " (" + std::to_string(maker) + " maker)"},
                                      {"Max |inventory|", std::to_string(s.max_abs_inventory)}});
    body += report::chart("PnL over the trading window (USD, end of each minute)", "minute", "USD", {pnl});
    body += report::chart("Inventory after each fill (shares)", "hours since midnight", "shares", {inv});
    body += report::chart("Fill prices", "hours since midnight", "USD", {buys, sells}, false);
    body += "<h2>Summary</h2>";
    std::vector<std::vector<std::string>> t;
    for (const auto& [k, v] : rows) t.push_back({k, v});
    body += report::table({"metric", "value"}, t);
    body += "<p class=\"muted\">PnL identity (total = spread + inventory + fees) is checked after every event. "
            "Diverted shares: real executions that went to our virtual orders instead; a large share means "
            "results ignore the market's reaction to us. Latency: market data " +
            std::to_string(c.market_data_ns / 1000) + " us, order entry " + std::to_string(c.order_entry_ns / 1000) +
            " us. Files: fills.csv, pnl_minutes.csv, summary.csv.</p>";
    report::write_file(dir + "/report.html", report::page("Backtest: " + strategy + " on " + symbol,
                                                          "itch_backtest, ITCH replay with virtual orders in the real queues", body));
}

}  // namespace hft::backtest
