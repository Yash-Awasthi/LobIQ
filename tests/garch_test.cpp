#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "core/optimize.hpp"
#include "core/philox.hpp"
#include "strategy/garch.hpp"

using namespace hft;
using namespace hft::strategy::vol;

namespace {

double normal(const rng::Stream& s, std::uint32_t i, std::uint32_t k) {
    return std::sqrt(-2 * std::log(s.uniform(i, k, 0))) * std::cos(2 * std::numbers::pi * s.uniform(i, k, 1));
}

// GARCH(1,1) returns; nu = 0 normal, else Student-t with integer nu scaled to unit variance.
std::vector<double> simulate(double omega, double alpha, double beta, int nu, std::size_t n, std::uint64_t seed) {
    const rng::Stream s(seed, 0, 0);
    std::vector<double> r(n);
    double h = omega / (1 - alpha - beta);
    for (std::size_t t = 0; t < n; ++t) {
        double z = normal(s, static_cast<std::uint32_t>(t), 0);
        if (nu > 0) {
            double chi = 0;
            for (int k = 0; k < nu; ++k) {
                const double g = normal(s, static_cast<std::uint32_t>(t), static_cast<std::uint32_t>(k + 1));
                chi += g * g;
            }
            z = z / std::sqrt(chi / nu) * std::sqrt((nu - 2.0) / nu);
        }
        r[t] = std::sqrt(h) * z;
        h = omega + alpha * r[t] * r[t] + beta * h;
    }
    return r;
}

}  // namespace

TEST(NelderMead, MinimisesTheRosenbrockFunction) {
    auto f = [](const std::vector<double>& x) { return 100 * std::pow(x[1] - x[0] * x[0], 2) + std::pow(1 - x[0], 2); };
    const auto r = opt::nelder_mead(f, {-1.2, 1.0}, 0.5, 10000, 1e-16);
    EXPECT_NEAR(r.x[0], 1, 1e-4);
    EXPECT_NEAR(r.x[1], 1, 1e-4);
}

TEST(Garch, MaximumLikelihoodRecoversANormalProcess) {
    const auto r = simulate(0.05, 0.1, 0.85, 0, 20000, 3);
    const GarchFit g = fit_garch(r, false);
    ASSERT_TRUE(g.ok);
    EXPECT_NEAR(g.alpha, 0.1, 0.03);
    EXPECT_NEAR(g.beta, 0.85, 0.04);
    EXPECT_NEAR(g.long_run(), 1.0, 0.15);
}

TEST(Garch, StudentTFitFindsTheTailsAndBeatsTheNormalLikelihood) {
    const auto r = simulate(0.02, 0.08, 0.9, 6, 20000, 5);
    const GarchFit t = fit_garch(r, true), n = fit_garch(r, false);
    ASSERT_TRUE(t.ok && n.ok);
    EXPECT_NEAR(t.nu, 6, 2);
    EXPECT_NEAR(t.alpha, 0.08, 0.03);
    EXPECT_NEAR(t.beta, 0.9, 0.04);
    EXPECT_GT(t.loglik, n.loglik + 100);
}

TEST(Garch, ConstantVarianceHasLittlePersistentAlpha) {
    const auto r = simulate(1.0, 0.0, 0.0, 0, 20000, 7);
    const GarchFit g = fit_garch(r, false);
    ASSERT_TRUE(g.ok);
    EXPECT_LT(g.alpha, 0.02);
    EXPECT_NEAR(g.long_run(), 1.0, 0.05);
}

TEST(Garch, MultiStepForecastDecaysToTheLongRunVariance) {
    GarchFit g;
    g.omega = 0.1, g.alpha = 0.1, g.beta = 0.8;  // long run 1, persistence 0.9
    EXPECT_DOUBLE_EQ(variance_ahead(g, 3.0, 1), 3.0);
    EXPECT_NEAR(variance_ahead(g, 3.0, 2), 1 + 0.9 * 2, 1e-12);
    EXPECT_NEAR(variance_ahead(g, 3.0, 200), 1, 1e-6);
    EXPECT_NEAR(variance_sum(g, 1.0, 10), 10, 1e-12);
    EXPECT_NEAR(g.half_life(), std::log(0.5) / std::log(0.9), 1e-12);
}

// The quoting filter on a grid equals the batch recursion on the same grid's changes.
TEST(Garch, OnlineFilterMatchesTheBatchRecursion) {
    OnlineGarch o;
    o.omega = 0.05, o.alpha = 0.1, o.beta = 0.85, o.dt_s = 1;
    const std::vector<double> mids = {100, 101, 101, 99, 100.5, 100.5, 102};
    std::vector<double> r;
    for (std::size_t i = 0; i < mids.size(); ++i) {
        o.update(i * 1'000'000'000ull + 500'000'000ull, mids[i]);  // one quote per interval, mid-interval
        if (i) r.push_back(mids[i - 1] - (i > 1 ? mids[i - 2] : mids[0]));
    }
    o.update(mids.size() * 1'000'000'000ull + 500'000'000ull, mids.back());
    r.push_back(mids.back() - mids[mids.size() - 2]);
    const auto h = garch_filter(r, 0.05, 0.1, 0.85, 0.05 / 0.05);
    EXPECT_NEAR(o.rate(), h.back(), 1e-12);
}

// A quote after a long silence folds the quiet intervals into one step, like the recursion over them.
TEST(Garch, OnlineFilterFoldsQuietIntervals) {
    OnlineGarch o;
    o.omega = 0.05, o.alpha = 0.1, o.beta = 0.85, o.dt_s = 1;
    o.update(500'000'000, 100);
    o.update(1'500'000'000, 102);
    o.update(2'500'000'000, 102);    // second boundary: change 2
    o.update(40'500'000'000, 102);   // the next 38 boundaries: no change
    std::vector<double> r = {0, 2};
    r.resize(40, 0.0);
    EXPECT_NEAR(o.rate(), garch_filter(r, 0.05, 0.1, 0.85, 1.0).back(), 1e-12);
}

TEST(Har, ConstantRealizedVarianceForecastsItself) {
    std::vector<std::vector<double>> days(3);
    for (auto& d : days)
        for (int t = 0; t < 200; ++t) d.push_back(2.0 + 0.5 * std::sin(t * 0.3));
    const Har h = fit_har(days);
    ASSERT_TRUE(h.ok());
    std::vector<double> flat(40, 2.0);
    EXPECT_NEAR(h.forecast(flat, 39), 2.0, 0.05);
}

TEST(Qlike, IsSmallestAtTheTrueVarianceAndAllowsZero) {
    EXPECT_LT(qlike(2.0, 2.0), qlike(2.0, 1.0));
    EXPECT_LT(qlike(2.0, 2.0), qlike(2.0, 4.0));
    EXPECT_TRUE(std::isfinite(qlike(0.0, 1.0)));
}

TEST(DieboldMariano, SignsTheBetterForecast) {
    std::vector<double> a, b;
    for (int i = 0; i < 500; ++i) a.push_back(1 + 0.3 * std::sin(i)), b.push_back(0.5 + 0.3 * std::sin(i * 1.7));
    EXPECT_GT(diebold_mariano(a, b, 5), 5);
    EXPECT_LT(diebold_mariano(b, a, 5), -5);
}
