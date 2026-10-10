#pragma once

// pm_exec --report: report.html plus timeline.csv and summary.csv for one replay.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "report/html.hpp"

namespace hft::pm {

struct ReportSample {
    double minute = 0, pnl_usd = 0, fees_usd = 0;
    std::uint64_t maker_fills = 0, taker_fills = 0, open_orders = 0;
};

struct ReportLatency {
    std::string stage;
    double p50 = 0, p99 = 0, p999 = 0;
    std::uint64_t count = 0;
};

struct ReportGroup {
    std::string name;
    std::uint64_t windows = 0;
    double open_s = 0;
    int max_edge_bp = 0;
};

inline void write_pm_report(const std::filesystem::path& dir, const std::string& recording,
                            const std::vector<std::pair<std::string, std::string>>& rows,
                            const std::vector<ReportSample>& samples, const std::vector<ReportLatency>& lat,
                            const std::vector<ReportGroup>& groups) {
    std::filesystem::create_directories(dir);
    auto value = [&](const std::string& k) {
        for (const auto& [key, v] : rows)
            if (key == k) return v;
        return std::string();
    };
    std::string csv = "minute,ledger_pnl_usd,fees_usd,maker_fills,taker_fills,open_orders\n";
    for (const ReportSample& s : samples)
        csv += report::num(s.minute, 3) + "," + report::num(s.pnl_usd, 6) + "," + report::num(s.fees_usd, 6) + "," +
               std::to_string(s.maker_fills) + "," + std::to_string(s.taker_fills) + "," + std::to_string(s.open_orders) + "\n";
    report::write_file((dir / "timeline.csv").string(), csv);
    std::string sum = "key,value\n";
    for (const auto& [k, v] : rows) sum += k + ",\"" + v + "\"\n";
    report::write_file((dir / "summary.csv").string(), sum);

    report::Series pnl("ledger PnL"), fees("fees paid", "var(--s2)"), maker("maker fills", "var(--s1)"),
        taker("taker fills", "var(--s3)"), open("open orders");
    for (const ReportSample& s : samples) {
        pnl.x.push_back(s.minute), pnl.y.push_back(s.pnl_usd);
        fees.x.push_back(s.minute), fees.y.push_back(-s.fees_usd);
        maker.x.push_back(s.minute), maker.y.push_back(static_cast<double>(s.maker_fills));
        taker.x.push_back(s.minute), taker.y.push_back(static_cast<double>(s.taker_fills));
        open.x.push_back(s.minute), open.y.push_back(static_cast<double>(s.open_orders));
    }
    open.step = true;
    std::string body = report::cards({{"Ledger PnL", report::money(std::atof(value("ledger_pnl_usd").c_str()))},
                                      {"Fees paid", report::money(std::atof(value("fees_usd").c_str()))},
                                      {"Maker fills", value("maker_fills")},
                                      {"Taker fills", value("taker_fills")},
                                      {"Arb windows / attempts", value("arb_windows") + " / " + value("arb_attempts")},
                                      {"Kill switch", value("kill")}});
    body += report::chart("Ledger PnL and fees over the recording (USD)", "minutes from start", "USD", {pnl, fees});
    body += report::chart("Fills, cumulative", "minutes from start", "fills", {maker, taker});
    body += report::chart("Open orders", "minutes from start", "orders", {open});

    body += "<h2>Latency per stage (ns)</h2>";
    std::vector<std::vector<std::string>> lt;
    for (const ReportLatency& l : lat)
        lt.push_back({l.stage, report::num(l.p50, 0), report::num(l.p99, 0), report::num(l.p999, 0), std::to_string(l.count)});
    body += report::table({"stage", "p50", "p99", "p99.9", "count"}, lt);

    body += "<h2>Arbitrage windows never attempted, by the filter that refused them last</h2>";
    std::vector<std::vector<std::string>> ft;
    const std::string f = value("arb_filtered");
    for (std::size_t at = 0; at < f.size();) {
        const std::size_t comma = std::min(f.find(',', at), f.size()), colon = f.find(':', at);
        if (colon < comma) ft.push_back({f.substr(at, colon - at), f.substr(colon + 1, comma - colon - 1)});
        at = comma + 1;
    }
    body += report::table({"filter", "windows"}, ft);

    if (!groups.empty()) {
        body += "<h2>Arbitrage windows by event group</h2>";
        std::vector<std::vector<std::string>> gt;
        for (const ReportGroup& g : groups)
            gt.push_back({g.name, std::to_string(g.windows), report::num(g.open_s, 1), std::to_string(g.max_edge_bp)});
        body += report::table({"group", "windows", "open seconds", "max edge (bp)"}, gt);
    }

    body += "<h2>Summary</h2>";
    std::vector<std::vector<std::string>> st;
    for (const auto& [k, v] : rows) st.push_back({k, v});
    body += report::table({"metric", "value"}, st);
    body += "<p class=\"muted\">Paper only: orders go to a simulated venue (latency, queue position from level-2 "
            "data, fees, settlement, injected faults). Level-2 data has no order ids, so queue position is estimated; "
            "other traders do not react to our orders. Files: timeline.csv, summary.csv.</p>";
    report::write_file((dir / "report.html").string(),
                       report::page("Paper execution replay", recording + " | decision_hash_v2 " + value("decision_hash_v2"), body));
}

// One run of pm_exec --sweep: its overrides ("k=0.5,size=50") and exec summary rows.
struct PmSweepRun {
    std::string label;
    std::vector<std::pair<std::string, std::string>> rows;
    std::string value(const std::string& k) const {
        for (const auto& [key, v] : rows)
            if (key == k) return v;
        return {};
    }
};

// DIR/index.html: runs ranked by ledger PnL, each linking its own report.
inline void write_pm_sweep_report(const std::filesystem::path& dir, const std::string& recording, std::vector<PmSweepRun> runs) {
    std::filesystem::create_directories(dir);
    auto pnl = [](const PmSweepRun& r) { return std::atof(r.value("ledger_pnl_usd").c_str()); };
    std::stable_sort(runs.begin(), runs.end(), [&](const PmSweepRun& a, const PmSweepRun& b) { return pnl(a) > pnl(b); });
    std::string rows;
    for (const PmSweepRun& r : runs)
        rows += "<tr><td><a href=\"" + report::esc(r.label) + "/report.html\">" + report::esc(r.label) + "</a></td><td>" +
                report::num(pnl(r)) + "</td><td>" + report::esc(r.value("fees_usd")) + "</td><td>" + report::esc(r.value("maker_fills")) +
                "</td><td>" + report::esc(r.value("taker_fills")) + "</td><td>" + report::esc(r.value("arb_attempts")) + "</td><td>" +
                report::esc(r.value("kill")) + "</td></tr>";
    const std::string body = "<h2>Runs, best ledger PnL first</h2><table><thead><tr><th>overrides</th><th>ledger PnL</th><th>fees</th>"
                             "<th>maker fills</th><th>taker fills</th><th>arb attempts</th><th>kill</th></tr></thead><tbody>" +
                             rows +
                             "</tbody></table><p class=\"muted\">USD. One recording is one sample: confirm a choice on "
                             "other recordings before trusting it.</p>";
    report::write_file((dir / "index.html").string(),
                       report::page("Paper sweep on " + recording, std::to_string(runs.size()) + " runs", body));
}

}  // namespace hft::pm
