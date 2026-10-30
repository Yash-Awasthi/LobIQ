#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "backtest/overfit.hpp"
#include "core/philox.hpp"

using namespace lobiq;
using namespace lobiq::backtest;

namespace {

std::vector<double> noise(std::size_t n, double mean, std::uint64_t seed) {
    const rng::Stream r(seed, 1, 0);
    std::vector<double> x(n);
    for (std::uint32_t t = 0; t < n; ++t)
        x[t] = mean + std::sqrt(-2 * std::log(r.uniform(t, 0))) * std::cos(2 * std::numbers::pi * r.uniform(t, 1));
    return x;
}

}  // namespace

TEST(StationaryBootstrap, BlocksHaveTheMeanLength) {
    const rng::Stream r(1, 0, 0);
    std::size_t starts = 0, n = 0;
    for (std::uint32_t b = 0; b < 50; ++b) {
        const auto idx = stationary_bootstrap(1000, 10, r, b);
        for (std::size_t t = 0; t < idx.size(); ++t) {
            ASSERT_LT(idx[t], 1000u);
            starts += t == 0 || idx[t] != (idx[t - 1] + 1) % 1000;
        }
        n += idx.size();
    }
    EXPECT_NEAR(static_cast<double>(n) / static_cast<double>(starts), 10, 1);
}

TEST(Spa, NoSkillIsNotSignificantAndRealSkillIs) {
    std::vector<std::vector<double>> d;
    for (std::uint64_t k = 0; k < 20; ++k) d.push_back(noise(500, 0, 10 + k));
    const SpaResult null = spa_test(d);
    EXPECT_GT(null.p_consistent, 0.05);
    EXPECT_LE(null.p_lower, null.p_consistent);
    EXPECT_LE(null.p_consistent, null.p_upper);
    d[7] = noise(500, 0.3, 99);
    const SpaResult skill = spa_test(d);
    EXPECT_LT(skill.p_consistent, 0.01);
    EXPECT_EQ(skill.best, 7u);
}

TEST(ModelConfidenceSet, KeepsTheBestAndDropsTheWorst) {
    const std::vector<double> means = {0, 0, 0.5, 1, 1};
    std::vector<std::vector<double>> loss;
    for (std::size_t k = 0; k < means.size(); ++k) loss.push_back(noise(400, means[k], 40 + k));
    const auto p = model_confidence_set(loss);
    EXPECT_GE(p[0], 0.1);
    EXPECT_GE(p[1], 0.1);
    EXPECT_LT(p[3], 0.05);
    EXPECT_LT(p[4], 0.05);
}

TEST(Pbo, NoiseOverfitsAndARealEdgeDoesNot) {
    std::vector<std::vector<double>> perf;
    for (std::uint64_t k = 0; k < 50; ++k) perf.push_back(noise(400, 0, 100 + k));
    const PboResult null = pbo_cscv(perf, 10);
    EXPECT_EQ(null.splits, 252u);  // 10 choose 5
    EXPECT_GT(null.pbo, 0.25);
    EXPECT_LT(null.pbo, 0.75);
    perf[3] = noise(400, 0.4, 7);
    const PboResult edge = pbo_cscv(perf, 10);
    EXPECT_LT(edge.pbo, 0.05);
    EXPECT_LT(edge.oos_loss, 0.05);
}

TEST(PurgedKFold, NoTrainingLabelOverlapsATestSample) {
    std::vector<std::uint64_t> start, end;
    for (std::uint64_t t = 0; t < 100; ++t) start.push_back(t * 10), end.push_back(t * 10 + 35);  // labels span 3.5 samples
    const auto folds = purged_kfold(start, end, 5, 20);
    ASSERT_EQ(folds.size(), 5u);
    for (const Fold& f : folds) {
        const std::uint64_t a = start[f.test.front()], b = end[f.test.back()];
        for (const std::size_t i : f.train) {
            EXPECT_TRUE(end[i] < a || start[i] > b + 20) << i;
        }
        EXPECT_EQ(f.test.size(), 20u);
    }
    EXPECT_EQ(folds[0].train.front(), 25u);  // the last test label ends at 225; embargo to 245; sample 25 starts at 250
}
