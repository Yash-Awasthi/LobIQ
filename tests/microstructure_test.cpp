#include "strategy/microstructure.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <tuple>
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

// Lead-lag: B's first move strictly after each A move, within the horizon, sign-adjusted.
TEST(LeadLag, AnswersAreTheFollowersNextMoveWithinTheHorizon) {
    using hft::strategy::micro::Move;
    const std::vector<Move> a = {{100, +1}, {10'000, -1}, {20'000, +1}, {5'000'000'000, +1}};
    const std::vector<Move> b = {{50, -1}, {100, -1}, {600, +1}, {15'000, -1}, {2'000'000'000, -1}};
    const auto r = lead_response(a, b, +1);
    EXPECT_EQ(r.leads, 4u);
    EXPECT_EQ(r.answered, 2u);  // 20'000's answer is 2 s away; 5 s has none
    EXPECT_EQ(r.same, 2u);      // +1 then 600 up (500 ns); -1 then 15'000 down (5 us)
    EXPECT_EQ(r.bins[0], 1u);   // under 1 us
    EXPECT_EQ(r.bins[1], 1u);   // 1-10 us
    const auto inv = lead_response(a, b, -1);
    EXPECT_EQ(inv.same, 0u);
    const auto placebo = lead_response(a, b, +1, 1'000);  // every A move 1 us later
    EXPECT_EQ(placebo.answered, 2u);                      // 1'100 and 11'000 both answered by 15'000 (down)
    EXPECT_EQ(placebo.same, 1u);                          // only the down move agrees
}

// Ogata thinning: a Hawkes process with intensity mu + alpha * sum exp(-beta (t - t_i)).
std::vector<double> simulate_hawkes(double mu, double alpha, double beta, double horizon, std::uint64_t seed) {
    std::uint64_t x = seed;
    auto uniform = [&] {
        x ^= x << 13, x ^= x >> 7, x ^= x << 17;
        return static_cast<double>(x >> 11) * 0x1.0p-53;
    };
    std::vector<double> t;
    double now = 0, excite = 0;  // excite: alpha * sum exp(-beta (now - t_i))
    for (;;) {
        const double bound = mu + excite;
        const double step = -std::log(1 - uniform()) / bound;
        now += step;
        excite *= std::exp(-beta * step);
        if (now >= horizon) return t;
        if (uniform() * bound <= mu + excite) t.push_back(now), excite += alpha;
    }
}

TEST(Hawkes, MaximumLikelihoodRecoversASimulatedProcess) {
    for (const auto& [mu, alpha, beta] : {std::tuple{1.0, 6.0, 10.0}, std::tuple{2.0, 1.0, 5.0}, std::tuple{0.5, 18.0, 20.0}}) {
        const auto t = simulate_hawkes(mu, alpha, beta, 4000, 7);
        const HawkesFit f = fit_hawkes(t, 4000);
        ASSERT_TRUE(f.ok);
        EXPECT_NEAR(f.alpha / f.beta, alpha / beta, 0.05) << "branching ratio, n = " << t.size();
        EXPECT_NEAR(f.beta, beta, 0.25 * beta);
        EXPECT_NEAR(f.mu, mu, 0.25 * mu);
    }
}

TEST(Hawkes, APoissonProcessHasNoExcitation) {
    const auto t = simulate_hawkes(3.0, 0.0, 1.0, 4000, 11);
    const HawkesFit f = fit_hawkes(t, 4000);
    ASSERT_TRUE(f.ok);
    EXPECT_LT(f.alpha / f.beta, 0.05);
    EXPECT_NEAR(f.mu, 3.0, 0.2);
}

// VPIN buckets: trades split across fixed-volume buckets; imbalance |buy - sell| / V per bucket.
TEST(Vpin, BucketsSplitTradesAndMeasureImbalance) {
    const std::vector<SignedTrade> t = {{150, +1, 10}, {50, -1, 11}, {300, +1, 12}, {100, -1, 13}, {30, +1, 14}};
    const auto b = volume_buckets(t, 200);
    ASSERT_EQ(b.size(), 3u);                  // 630 shares: three full buckets, 30 left over
    EXPECT_DOUBLE_EQ(b[0].imbalance, 0.5);    // 150 buy + 50 sell
    EXPECT_DOUBLE_EQ(b[0].mid, 11);           // mid of the trade that closed it
    EXPECT_DOUBLE_EQ(b[1].imbalance, 1.0);    // 200 of the 300 buy
    EXPECT_DOUBLE_EQ(b[2].imbalance, 0.0);    // 100 buy + 100 sell
    EXPECT_DOUBLE_EQ(b[2].mid, 13);
}
