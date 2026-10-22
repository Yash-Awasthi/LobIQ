#include <gtest/gtest.h>

#include <vector>

#include "strategy/realism.hpp"

namespace rl = hft::strategy::realism;

namespace {

std::vector<double> ramp(int n, double shift = 0) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(i * 0.01 + shift);
    return v;
}

}  // namespace

TEST(Realism, DistancesOfIdenticalShiftedAndDisjointSamples) {
    const auto a = ramp(1000), b = ramp(1000, 0.5), far = ramp(1000, 100);
    EXPECT_NEAR(rl::wasserstein(a, a), 0, 1e-12);
    EXPECT_NEAR(rl::total_variation(a, a), 0, 1e-12);
    double m, sd;
    rl::mean_sd(a, m, sd);
    EXPECT_NEAR(rl::wasserstein(a, b), 0.5 / sd, 1e-6);  // a shift moves every quantile by itself
    EXPECT_NEAR(rl::total_variation(a, far), 1, 1e-12);
    EXPECT_NEAR(rl::total_variation(a, b), 0.05, 0.02);  // about the shift over the range
}

TEST(Realism, AutocorrelationAndKurtosis) {
    std::vector<double> alt;
    for (int i = 0; i < 100; ++i) alt.push_back(i % 2 ? 1 : -1);
    EXPECT_NEAR(rl::acf(alt, 1), -0.99, 1e-9);  // -(n-1)/n for a zero-mean alternation
    EXPECT_NEAR(rl::acf(alt, 2), 0.98, 1e-9);
    EXPECT_NEAR(rl::kurtosis(alt), 1, 1e-9);   // only two values: the smallest kurtosis there is
}

TEST(Realism, ReservoirKeepsEverythingUpToItsCapThenASample) {
    rl::Reservoir small(10), big(1000);
    for (int i = 0; i < 500; ++i) small.add(i), big.add(i);
    EXPECT_EQ(small.v.size(), 10u);
    EXPECT_EQ(small.seen(), 500u);
    EXPECT_EQ(big.v.size(), 500u);
    EXPECT_EQ(big.v[499], 499);
}
