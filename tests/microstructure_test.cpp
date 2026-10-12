#include "strategy/microstructure.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace hft::strategy::micro;
using hft::book::Bbo;

Bbo quote(std::uint32_t bid, std::uint32_t bq, std::uint32_t ask, std::uint32_t aq) {
    Bbo b;
    b.bid_px = bid, b.bid_qty = bq, b.ask_px = ask, b.ask_qty = aq;
    return b;
}

// The cases of Cont, Kukanov and Stoikov (2014), equation (3).
TEST(Ofi, FollowsThePaperCaseByCase) {
    const Bbo p = quote(100, 500, 101, 400);
    EXPECT_EQ(ofi_event(p, quote(100, 700, 101, 400)), 200);   // bid size up
    EXPECT_EQ(ofi_event(p, quote(100, 500, 101, 100)), 300);   // ask size down
    EXPECT_EQ(ofi_event(p, quote(101, 50, 102, 400)), 450);    // both move up: new bid and old ask count
    EXPECT_EQ(ofi_event(p, quote(99, 900, 101, 400)), -500);   // bid level gone: old size leaves
    EXPECT_EQ(ofi_event(p, quote(100, 500, 101, 400)), 0);    // nothing changed
    EXPECT_EQ(ofi_event(p, quote(100, 500, 102, 900)), 400);   // ask level gone
    EXPECT_EQ(ofi_event(p, quote(100, 500, 100, 30)), -30);    // ask improves (locked)
}

TEST(MicroPrice, StatesMirrorImbalanceWithinASpread) {
    EXPECT_EQ(state_of(1, 0.0), 0);
    EXPECT_EQ(state_of(1, 0.999), kBuckets - 1);
    EXPECT_EQ(state_of(1, 1.0), kBuckets - 1);
    EXPECT_EQ(state_of(2, 0.05), kBuckets);
    for (int x = 0; x < kStates; ++x) {
        EXPECT_EQ(mirror(mirror(x)), x);
        EXPECT_EQ(mirror(x) / kBuckets, x / kBuckets);
    }
}

// A chain where the mid always moves one tick toward the heavy side and the book then resets
// to balance: G is +1 in the top bucket, -1 in the bottom one, 0 at balance.
TEST(MicroPrice, AdjustmentFollowsAKnownChain) {
    Counts c;
    const int hi = state_of(1, 0.95), mid = state_of(1, 0.55), lo = state_of(1, 0.05);
    for (int i = 0; i < 100; ++i) {
        c.add(hi, mid, +1.0);
        c.add(mid, hi, 0.0);
        c.add(mid, lo, 0.0);
    }
    const auto g = adjustment(c);
    EXPECT_NEAR(g[static_cast<std::size_t>(hi)], 1.0, 1e-9);
    EXPECT_NEAR(g[static_cast<std::size_t>(lo)], -1.0, 1e-9);
    EXPECT_NEAR(g[static_cast<std::size_t>(mid)] + g[static_cast<std::size_t>(mirror(mid))], 0.0, 1e-9);
    for (int x = 0; x < kStates; ++x)
        EXPECT_NEAR(g[static_cast<std::size_t>(x)], -g[static_cast<std::size_t>(mirror(x))], 1e-9) << x;
}

TEST(MicroPrice, UnseenStatesGetNoAdjustment) {
    Counts c;
    c.add(state_of(1, 0.9), state_of(1, 0.5), 0.5);
    const auto g = adjustment(c);
    EXPECT_EQ(g[static_cast<std::size_t>(state_of(2, 0.3))], 0.0);
}

TEST(Ols, RecoversALineAndScoresOutOfSample) {
    std::vector<double> x, y;
    for (int i = 0; i < 50; ++i) x.push_back(i), y.push_back(3 + 2 * i);
    const Fit f = ols(x, y);
    EXPECT_NEAR(f.a, 3, 1e-9);
    EXPECT_NEAR(f.b, 2, 1e-9);
    EXPECT_NEAR(f.r2, 1, 1e-12);
    std::vector<double> mean(y.size(), 0);
    double my = 0;
    for (double v : y) my += v;
    for (double& v : mean) v = my / static_cast<double>(y.size());
    EXPECT_NEAR(r2_of(y, y), 1, 1e-12);
    EXPECT_NEAR(r2_of(mean, y), 0, 1e-12);
}
