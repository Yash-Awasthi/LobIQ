#include <gtest/gtest.h>
#include <zstd.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "backtest/registry.hpp"
#include "backtest/report.hpp"
#include "backtest/strategies.hpp"
#include "data/gzip.hpp"
#include "data/store.hpp"
#include "sources/queue_reactive.hpp"
#include "temp_dir.hpp"

using namespace hft;
using namespace hft::backtest;

namespace {

// The committed fixture as a store. Its clock runs from 04:00 for about 20 s, so the window
// quotes for 12 s and then flattens.
struct FixtureStore {
    TempDir dir{"bt_store"};
    FixtureStore() {
        std::ifstream f(HFT_SOURCE_DIR "/tests/data/flow-7-4-20k.itch.zst", std::ios::binary);
        const std::vector<std::uint8_t> comp((std::istreambuf_iterator<char>(f)), {});
        std::vector<std::uint8_t> raw(ZSTD_getFrameContentSize(comp.data(), comp.size()));
        ZSTD_decompress(raw.data(), raw.size(), comp.data(), comp.size());
        data::StoreWriter w(dir.path, 1 << 24, 1 << 20);
        itch::Frame fr{};
        std::uint64_t seq = 0;
        for (std::size_t pos = 0, k; (k = itch::next_frame(raw.data() + pos, raw.size() - pos, fr));
             pos += k)
            w.append(load_be16(fr.data + 1), ++seq, itch::detail::read_header(fr.data).timestamp,
                     fr.data, fr.size);
        w.finish();
    }
};

Config window() {
    Config c;
    c.start_ns = 4ull * 3600 * 1'000'000'000;
    c.stop_ns = c.start_ns + 12'000'000'000ull;
    c.end_ns = c.start_ns + 25'000'000'000ull;
    c.exchange.maker_rebate = 2000;
    c.exchange.taker_fee = 3000;
    c.market_data_ns = 50'000;
    c.order_entry_ns = 20'000;
    c.risk.collar_ticks = 1000;
    return c;
}

}  // namespace

TEST(Backtest, ZeroStrategyHasZeroPnl) {
    FixtureStore fx;
    Zero z;
    Backtest<Zero> bt(window(), z);
    const Summary s = bt.run(fx.dir.path.string(), 1, {3});
    EXPECT_EQ(s.total, 0);
    EXPECT_EQ(s.fills, 0u);
    EXPECT_EQ(s.orders, 0u);
    EXPECT_GT(s.events, 1000u);
}

TEST(Backtest, NaiveJoinTradesKeepsTheIdentityAndFlattens) {
    FixtureStore fx;
    NaiveJoin n;
    Backtest<NaiveJoin> bt(window(), n);
    const Summary s = bt.run(fx.dir.path.string(), 1, {3});
    EXPECT_GT(s.fills, 10u);
    EXPECT_EQ(s.total, s.spread + s.inventory_pnl + s.fees);
    EXPECT_LE(s.max_abs_inventory, 600);
    EXPECT_EQ(s.end_inventory, 0);
}

TEST(Backtest, RandomTakingLosesAboutHalfTheSpreadPlusFees) {
    FixtureStore fx;
    RandomTaker r;
    r.rate = 0.02;
    Backtest<RandomTaker> bt(window(), r);
    const Summary s = bt.run(fx.dir.path.string(), 1, {3});
    ASSERT_GT(s.volume, 0);
    EXPECT_LT(s.spread, 0);  // every take pays half the spread
    EXPECT_LT(s.fees, 0);
}

// The GLFT quotes widen against inventory: a long position lowers both quotes.
TEST(Strategies, AvellanedaStoikovSkewsAgainstInventory) {
    book::TickBook b;
    b.add(1, Side::Buy, 100, 10'0000, 1);
    b.add(2, Side::Sell, 100, 10'0500, 2);
    std::vector<Working> w;
    AvellanedaStoikov as;
    as.var = 4.0;
    as.last_mid = 1002.5;
    as.last_ts = 1;
    Desired flat, longer;
    as.decide(View{2, b, nullptr, 0, 1002.5, 0, w, true}, flat);
    as.last_ts = 1;
    as.decide(View{2, b, nullptr, 0, 1002.5, 300, w, true}, longer);
    EXPECT_LT(longer.bid_px, flat.bid_px);
    EXPECT_LE(longer.ask_px, flat.ask_px);
    EXPECT_LT(flat.bid_px, flat.ask_px);
    EXPECT_LE(flat.ask_px, 10'0500u + 1000u);
}

// A distance cap keeps quotes near the mid when the fitted intensity would put them far away.
TEST(Strategies, AvellanedaStoikovDistanceCap) {
    book::TickBook b;
    b.add(1, Side::Buy, 100, 10'0000, 1);
    b.add(2, Side::Sell, 100, 10'0500, 2);
    std::vector<Working> w;
    Desired far, near;
    for (const double cap : {0.0, 3.0}) {
        AvellanedaStoikov as;
        as.k = 0.01;  // flat intensity: quotes tens of ticks from the mid
        as.max_dist_ticks = cap;
        as.var = 4.0;
        as.last_mid = 1002.5;
        as.last_ts = 1;
        as.decide(View{2, b, nullptr, 0, 1002.5, 0, w, true}, cap ? near : far);
    }
    EXPECT_LT(far.bid_px, 10'0000u - 2000u);
    EXPECT_GE(near.bid_px, 10'0000u - 500u);
    EXPECT_LE(near.ask_px, 10'0500u + 500u);
}

// A bid fill after 10 s of quoting raises the fitted intensity; without `online` A, k stay.
TEST(Strategies, AvellanedaStoikovRecalibratesOnItsFills) {
    book::TickBook b;
    b.add(1, Side::Buy, 100, 10'0000, 1);
    b.add(2, Side::Sell, 100, 10'0500, 2);
    std::vector<Working> w;
    for (const bool online : {false, true}) {
        AvellanedaStoikov as;
        as.A = 0.01, as.k = 1.5, as.online = online, as.online_prior_s = 1;
        Desired d1, d2;
        as.decide(View{1'000'000'000, b, nullptr, 0, 1002.5, 0, w, true}, d1);
        ASSERT_GT(d1.bid_qty, 0u);
        as.decide(View{11'000'000'000, b, nullptr, 0, 1002.5, 100, w, true}, d2);
        if (online) {
            EXPECT_GT(as.A * std::exp(-as.k * 2.5), 0.01 * std::exp(-1.5 * 2.5));
        } else {
            EXPECT_EQ(as.A, 0.01);
            EXPECT_EQ(as.k, 1.5);
        }
    }
}

// In-flight takes count against the position limit until their reports come back.
TEST(Backtest, PositionLimitHoldsWithTakesInFlight) {
    FixtureStore fx;
    RandomTaker r;
    r.rate = 0.5;
    r.size = 300;
    Config c = window();
    c.risk.max_position = 600;
    c.order_entry_ns = 2'000'000;  // long round trip: many decisions per report
    c.risk.max_msgs_per_s = 1e6;
    c.risk.burst = 1e6;
    Backtest<RandomTaker> bt(c, r);
    const Summary s = bt.run(fx.dir.path.string(), 1, {3});
    ASSERT_GT(s.volume, 0);
    EXPECT_LE(s.max_abs_inventory, 600);
}

TEST(Backtest, HysteresisCutsQuoteChurn) {
    FixtureStore fx;
    auto orders = [&](std::uint32_t h) {
        AvellanedaStoikov as;
        as.end_ns = window().end_ns;
        Backtest<AvellanedaStoikov> bt(window(), as);
        bt.set_hysteresis(h);
        return bt.run(fx.dir.path.string(), 1, {3}).orders;
    };
    const auto churn = orders(0), calm = orders(3);
    EXPECT_GT(churn, 0u);
    EXPECT_LT(calm, churn);
}

// Table layout of archive/research/dp.py (research-archive tag) export: [q][bid queue][ask queue][imbalance][spread][signal].
namespace {

// Writes a small policy table in the archive/research/dp.py (research-archive tag) layout: [q][bid queue][ask queue]
// [imbalance][spread][signal], signal weights w on two features.
std::string write_table(const TempDir& dir, double w0, double w1) {
    const std::string path = (dir.path / "t.bin").string();
    // The header counts queue states as (bid, ask) pairs: 16.
    const std::int32_t Q = 1, nq = 4, ni = 5, ns = 3, ng = 3, k = 2;
    std::vector<std::uint8_t> table(static_cast<std::size_t>(2 * Q + 1) * nq * nq * ni * ns * ng,
                                    0);
    auto at = [&](int q, int pb, int pa, int i, int s, int g) -> std::uint8_t& {
        return table[((((static_cast<std::size_t>(q + Q) * nq + pb) * nq + pa) * ni + i) * ns + s) *
                         ng +
                     g];
    };
    at(0, 0, 0, 2, 0, 1) = 4;  // flat, no orders, balanced, one tick, flat signal: both at best
    at(1, 0, 0, 2, 0, 1) = 1;  // one lot long: bid out, ask at best (action = bid * 3 + ask)
    at(0, 0, 0, 4, 0, 1) = 9;  // strong bid imbalance: take
    {
        std::ofstream f(path, std::ios::binary);
        f.write("HFTDP001", 8);
        const std::int32_t h[6] = {Q, nq * nq, ni, ns, ng, k};
        f.write(reinterpret_cast<const char*>(h), sizeof h);
        const double imb[4] = {-0.6, -0.2, 0.2, 0.6}, spr[2] = {1.5, 3.5}, sig[2] = {-0.5, 0.5};
        const double mu[2] = {0, 0}, sd[2] = {1, 1}, w[2] = {w0, w1};
        for (const auto* a : {imb}) f.write(reinterpret_cast<const char*>(a), sizeof imb);
        f.write(reinterpret_cast<const char*>(spr), sizeof spr);
        f.write(reinterpret_cast<const char*>(sig), sizeof sig);
        f.write(reinterpret_cast<const char*>(mu), sizeof mu);
        f.write(reinterpret_cast<const char*>(sd), sizeof sd);
        f.write(reinterpret_cast<const char*>(w), sizeof w);
        f.write(reinterpret_cast<const char*>(table.data()),
                static_cast<std::streamsize>(table.size()));
    }
    return path;
}

}  // namespace

TEST(Strategies, DpPolicyLooksUpTheExportedLayout) {
    TempDir dir("dp");
    const std::string path = write_table(dir, 0, 0);
    DpPolicy p;
    p.load(path);
    book::TickBook b;
    b.add(1, Side::Buy, 100, 10'0000, 1);
    b.add(2, Side::Sell, 100, 10'0100, 2);
    std::vector<Working> w;
    double f[2] = {1.0, 0.0};  // spread ticks, imbalance
    Desired d0, d1, d2;
    p.decide(View{1, b, f, 2, 1000.5, 0, w, true}, d0);
    EXPECT_EQ(d0.bid_px, 10'0000u);
    EXPECT_EQ(d0.ask_px, 10'0100u);
    p.decide(View{1, b, f, 2, 1000.5, 100, w, true}, d1);
    EXPECT_EQ(d1.bid_qty, 0u);
    EXPECT_EQ(d1.ask_px, 10'0100u);
    f[1] = 0.9;
    p.decide(View{1, b, f, 2, 1000.5, 0, w, true}, d2);
    EXPECT_EQ(d2.take_side, 1);
    EXPECT_EQ(d2.take_limit, 10'0100u);
}

TEST(Strategies, ExtendedGuardsAndTakes) {
    TempDir dir("ext");
    Extended e;
    e.dp.load(write_table(dir, 0.0, 10.0));  // signal = 10 x imbalance, in ticks
    e.vol_limit = 1.0;
    book::TickBook b;
    b.add(1, Side::Buy, 100, 10'0000, 1);
    b.add(2, Side::Sell, 100, 10'0100, 2);
    std::vector<Working> w;
    double f[20] = {};
    f[0] = 1;
    Desired calm, toxic, strong;
    e.decide(View{1, b, f, 20, 1000.5, 0, w, true}, calm);
    EXPECT_EQ(calm.bid_qty, 100u);  // balanced book: both quotes, no take
    EXPECT_EQ(calm.take_side, 0);
    f[19] = 2.0;  // volatility above the limit
    e.decide(View{1, b, f, 20, 1000.5, 0, w, true}, toxic);
    EXPECT_EQ(toxic.bid_qty + toxic.ask_qty, 0u);
    f[19] = 0;
    f[1] = 0.1;  // signal 1 tick > half spread 0.5 + fee 0.3 + margin 0.1
    e.decide(View{1, b, f, 20, 1000.5, 0, w, true}, strong);
    EXPECT_EQ(strong.take_side, 1);
    e.taking = false;
    Desired off;
    e.decide(View{1, b, f, 20, 1000.5, 0, w, true}, off);
    EXPECT_EQ(off.take_side, 0);
}

// At a half-penny tick the strategy's mid (in ticks) must use the exchange tick, or every
// quote lands outside the price collar.
TEST(Backtest, NaiveJoinQuotesAtTheHalfPennyTick) {
    sources::QrParams p;
    p.K = 3, p.N = 20, p.tick = 50, p.p_ref = 200'025, p.theta = 0.5;
    const auto cells = static_cast<std::size_t>(p.K * (p.N + 1));
    p.L.assign(cells, 2.0), p.C.assign(cells, 0), p.M.assign(cells, 0.5), p.init.assign(cells, 0);
    for (int i = 0; i < p.K; ++i)
        for (int n = 0; n <= p.N; ++n) {
            const auto c = static_cast<std::size_t>(i * (p.N + 1) + n);
            p.C[c] = 0.4 * n;
            p.init[c] = n >= 2 && n <= 4 ? 1.0 / 3 : 0.0;
        }
    p.start_ns = 34'200'000'000'000ull, p.end_ns = p.start_ns + 120'000'000'000ull;
    std::vector<std::uint8_t> raw;
    sources::QueueReactive(p, 1).day(raw);
    TempDir dir{"bt_half"};
    {
        data::StoreWriter w(dir.path, 1 << 24, 1 << 20);
        itch::Frame fr{};
        std::uint64_t seq = 0;
        for (std::size_t pos = 0, k; (k = itch::next_frame(raw.data() + pos, raw.size() - pos, fr));
             pos += k)
            w.append(load_be16(fr.data + 1), ++seq, itch::detail::read_header(fr.data).timestamp,
                     fr.data, fr.size);
        w.finish();
    }
    Config c;
    c.start_ns = p.start_ns + 1'000'000'000ull;
    c.stop_ns = p.end_ns - 20'000'000'000ull;
    c.end_ns = p.end_ns - 1'000'000'000ull;
    c.exchange.tick = 50;
    NaiveJoin s;
    Backtest<NaiveJoin> bt(c, s);
    const Summary r = bt.run(dir.path.string(), 1, {});
    EXPECT_EQ(r.rejects, 0u);
    EXPECT_GT(r.fills, 0u);
}

TEST(Registry, FindsStrategiesByNameAndPrefix) {
    ASSERT_NE(find_strategy("naive"), nullptr);
    ASSERT_NE(find_strategy("imbalance"), nullptr);
    const StrategyEntry* dp = find_strategy("dp:some/policy.bin");
    ASSERT_NE(dp, nullptr);
    EXPECT_EQ(dp->name, "dp");
    EXPECT_EQ(find_strategy("dp"), nullptr);  // a prefix strategy needs its argument
    EXPECT_EQ(find_strategy("nope"), nullptr);
}

TEST(Registry, ARegisteredStrategyRunsLikeTheTypeItself) {
    FixtureStore fx;
    RunSpec r;
    r.config = window();
    r.store = fx.dir.path.string();
    r.target = 1;
    r.index = {3};
    Params p;
    p.kv["size"] = 100;
    const Summary via = find_strategy("naive")->run(r, p);
    NaiveJoin n{100, 500};
    const Summary direct = run_strategy(r, n);
    EXPECT_GT(via.fills, 10u);
    EXPECT_EQ(via.total, direct.total);
    EXPECT_EQ(via.fill_ts, direct.fill_ts);
    const Summary imb = find_strategy("imbalance")->run(r, p);
    EXPECT_EQ(imb.total, imb.spread + imb.inventory_pnl + imb.fees);
    EXPECT_EQ(imb.end_inventory, 0);
}

// leader with the leader ignored is the imbalance maker at threshold 0; heeding it only withdraws quotes.
TEST(Registry, LeaderReducesToImbalanceWhenTheLeaderIsIgnored) {
    FixtureStore fx;
    RunSpec r;
    r.config = window();
    r.store = fx.dir.path.string();
    r.target = 1;
    r.index = {3};
    Params imb, ignored, heeded;
    imb.kv["threshold"] = 0;
    ignored.kv["lead_bp"] = 1e9;
    heeded.kv["lead_bp"] = 0;
    const Summary a = find_strategy("imbalance")->run(r, imb);
    const Summary b = find_strategy("leader")->run(r, ignored);
    const Summary c = find_strategy("leader")->run(r, heeded);
    EXPECT_GT(a.fills, 0u);
    EXPECT_EQ(b.total, a.total);
    EXPECT_EQ(b.fill_ts, a.fill_ts);
    EXPECT_EQ(c.total, c.spread + c.inventory_pnl + c.fees);
    EXPECT_LE(c.fills, a.fills);
}

TEST(Report, WritesTheReportAndItsData) {
    FixtureStore fx;
    NaiveJoin n;
    Backtest<NaiveJoin> bt(window(), n);
    const Summary s = bt.run(fx.dir.path.string(), 1, {3});
    TempDir out{"bt_report"};
    write_report(out.path.string(), "SYM", "naive", window(), s);
    for (const char* f : {"report.html", "fills.csv", "pnl_minutes.csv", "summary.csv"})
        EXPECT_TRUE(std::filesystem::exists(out.path / f)) << f;
    std::ifstream h(out.path / "fills.csv");
    std::size_t lines = 0;
    for (std::string l; std::getline(h, l);) ++lines;
    EXPECT_EQ(lines, s.fills + 1);
    std::ifstream page(out.path / "report.html");
    const std::string html((std::istreambuf_iterator<char>(page)), {});
    EXPECT_NE(html.find("<svg"), std::string::npos);
    EXPECT_EQ(html.find("<script"), std::string::npos);  // self-contained, nothing to execute
}

TEST(Report, SweepLabelsAndRanking) {
    SweepRun a, b;
    a.params = {{"gamma", 0.1}, {"size", 200}};
    b.params = {{"gamma", 0.25}, {"size", 100}};
    EXPECT_EQ(a.label(), "gamma=0.1,size=200");
    a.summary.total = 5'000'000, b.summary.total = 9'000'000;  // micro-dollars
    TempDir out{"sweep_report"};
    write_sweep_report(out.path.string(), "SYM", "naive", {a, b});
    std::ifstream page(out.path / "index.html");
    const std::string html((std::istreambuf_iterator<char>(page)), {});
    const std::size_t pb = html.find("href=\"gamma=0.25,size=100/report.html\""), pa = html.find("href=\"gamma=0.1,size=200/report.html\"");
    ASSERT_NE(pa, std::string::npos);
    ASSERT_NE(pb, std::string::npos);
    EXPECT_LT(pb, pa);  // best total first
}

TEST(Report, SeveralSymbolsAreTotalledPerParameterSet) {
    std::vector<SweepRun> runs(4);
    const char* sym[] = {"AAA", "BBB", "AAA", "BBB"};
    for (int i = 0; i < 4; ++i)
        runs[i].symbol = sym[i], runs[i].params = {{"size", i < 2 ? 100.0 : 200.0}}, runs[i].summary.total = (i + 1) * 1'000'000;
    EXPECT_EQ(runs[0].label(), "AAA,size=100");
    TempDir out{"sweep_symbols"};
    write_sweep_report(out.path.string(), "AAA,BBB", "naive", runs);
    std::ifstream page(out.path / "index.html");
    const std::string html((std::istreambuf_iterator<char>(page)), {});
    EXPECT_NE(html.find("<td>size=200</td><td>7.00</td><td>2</td>"), std::string::npos);  // 3 + 4 USD
    EXPECT_NE(html.find("<td>size=100</td><td>3.00</td><td>2</td>"), std::string::npos);
}
