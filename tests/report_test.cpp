#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "pm/report.hpp"
#include "report/html.hpp"
#include "temp_dir.hpp"

using namespace hft;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), {}};
}

}  // namespace

TEST(Html, EscapesAndFormatsMoney) {
    EXPECT_EQ(report::esc("<a href=\"x\">&</a>"), "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;");
    EXPECT_EQ(report::money(-624.444), "-$624.44");
    EXPECT_EQ(report::money(3.5), "$3.50");
}

TEST(Html, TickStepsAreOneTwoOrFiveTimesAPowerOfTen) {
    EXPECT_DOUBLE_EQ(report::tick_step(0, 100, 5), 20);
    EXPECT_DOUBLE_EQ(report::tick_step(0, 1, 8), 0.2);
    EXPECT_DOUBLE_EQ(report::tick_step(-3, 7, 5), 2);
}

TEST(Html, ChartsHandleEmptyAndFlatSeries) {
    report::Series empty("e");
    EXPECT_NE(report::chart("t", "x", "y", {empty}).find("no data"), std::string::npos);
    report::Series flat("f");
    flat.x = {0, 1, 2}, flat.y = {5, 5, 5};
    const std::string svg = report::chart("t", "x", "y", {flat});
    EXPECT_NE(svg.find("<path"), std::string::npos);
    EXPECT_EQ(svg.find("nan"), std::string::npos);
    EXPECT_EQ(svg.find("inf"), std::string::npos);
    EXPECT_EQ(svg.find(">-0<"), std::string::npos);
}

TEST(PmReport, WritesTimelineSummaryAndPage) {
    TempDir out{"pm_report"};
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"ledger_pnl_usd", "-1.250000"}, {"maker_fills", "3"}, {"arb_filtered", "stale:4,edge:2"},
        {"decision_hash_v2", "00ff"}, {"group<x>", "a<b"}};
    std::vector<pm::ReportSample> samples = {{0, 0, 0, 0, 0, 0}, {0.5, -1.25, 0.1, 3, 1, 2}};
    pm::write_pm_report(out.path, "rec", rows, samples, {{"parse", 500, 900, 1200, 10}}, {{"evt", 2, 1.5, 30}});
    const std::string timeline = slurp(out.path / "timeline.csv");
    EXPECT_EQ(std::count(timeline.begin(), timeline.end(), '\n'), 3);
    EXPECT_NE(timeline.find("0.500,-1.250000,0.100000,3,1,2"), std::string::npos);
    const std::string html = slurp(out.path / "report.html");
    EXPECT_NE(html.find("-$1.25"), std::string::npos);
    EXPECT_NE(html.find("<td>stale</td><td>4</td>"), std::string::npos);
    EXPECT_NE(html.find("a&lt;b"), std::string::npos);  // values are escaped
    EXPECT_EQ(html.find("<script"), std::string::npos);
}

TEST(PmReport, SweepIndexRanksByLedgerPnl) {
    TempDir out{"pm_sweep"};
    std::vector<pm::PmSweepRun> runs = {{"k=10", {{"ledger_pnl_usd", "-0.5"}, {"kill", "none"}}},
                                        {"k=30", {{"ledger_pnl_usd", "1.25"}, {"kill", "none"}}}};
    pm::write_pm_sweep_report(out.path, "rec", runs);
    const std::string html = slurp(out.path / "index.html");
    const std::size_t a = html.find("href=\"k=30/report.html\""), b = html.find("href=\"k=10/report.html\"");
    ASSERT_NE(a, std::string::npos);
    ASSERT_NE(b, std::string::npos);
    EXPECT_LT(a, b);
}
