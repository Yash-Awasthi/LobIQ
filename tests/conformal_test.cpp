#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/philox.hpp"
#include "strategy/conformal.hpp"

using namespace hft;
using namespace hft::strategy::conformal;

TEST(SplitConformal, TakesTheFiniteSampleRank) {
    const std::vector<double> s = {5, 1, 9, 3, 7, 2, 8, 4, 6};
    EXPECT_EQ(split_quantile(s, 0.1), 9);   // ceil(10 * 0.9) = 9th smallest
    EXPECT_EQ(split_quantile(s, 0.2), 8);   // ceil(10 * 0.8) = 8th
    EXPECT_EQ(split_quantile(s, 0.5), 5);
    EXPECT_TRUE(std::isinf(split_quantile({1, 2}, 0.1)));  // two points cannot promise 90%
}

TEST(SplitConformal, CoversNewExchangeablePoints) {
    const rng::Stream r(3, 0, 0);
    std::vector<double> cal;
    for (std::uint32_t i = 0; i < 1000; ++i) cal.push_back(-std::log(r.uniform(i, 0)));
    const double q = split_quantile(cal, 0.1);
    double hit = 0;
    for (std::uint32_t i = 0; i < 20000; ++i) hit += -std::log(r.uniform(i, 1)) <= q;
    EXPECT_NEAR(hit / 20000, 0.9, 0.02);
}

// Adaptive conformal keeps the long-run miss rate at alpha even when the scale triples halfway.
TEST(AdaptiveConformal, KeepsCoverageThroughAShift) {
    const rng::Stream r(4, 0, 0);
    Adaptive a(0.1, 0.01, 300);
    double late_miss = 0;
    for (std::uint32_t i = 0; i < 40000; ++i) {
        const double scale = i < 20000 ? 1 : 3;
        const double score = scale * -std::log(r.uniform(i, 0));
        const double q = a.quantile();
        if (i >= 20000) late_miss += score > q;
        a.observe(score, q);
    }
    EXPECT_NEAR(a.miss_rate(), 0.1, 0.01);
    EXPECT_NEAR(late_miss / 20000, 0.1, 0.015);
}

// The in-place window update gives the same quantile as sorting the last `window` scores.
TEST(AdaptiveConformal, WindowQuantileMatchesASortedCopy) {
    const rng::Stream r(6, 0, 0);
    Adaptive a(0.2, 0.0, 50);
    std::vector<double> all;
    for (std::uint32_t i = 0; i < 1000; ++i) {
        const double s = std::floor(10 * r.uniform(i, 0));  // ties exercise both shift directions
        a.observe(s, a.quantile());
        all.push_back(s);
        std::vector<double> w(all.end() - static_cast<std::ptrdiff_t>(std::min<std::size_t>(all.size(), 50)), all.end());
        std::sort(w.begin(), w.end());
        const double rank = std::ceil((static_cast<double>(w.size()) + 1) * 0.8);
        const double want = rank > static_cast<double>(w.size()) ? INFINITY : w[static_cast<std::size_t>(rank) - 1];
        ASSERT_EQ(a.quantile(), want) << i;
    }
}
