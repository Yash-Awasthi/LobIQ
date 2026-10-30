#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "strategy/hawkes_mv.hpp"
#include "strategy/microstructure.hpp"

using namespace hft::strategy;

namespace {

hawkes::Model model(std::vector<double> mu, std::vector<double> alpha, double beta) {
    hawkes::Model m;
    m.D = mu.size(), m.mu = std::move(mu), m.alpha = std::move(alpha), m.beta = beta, m.ok = true;
    return m;
}

}  // namespace

TEST(HawkesMv, SpectralRadiusOfAPeriodicMatrix) {
    EXPECT_NEAR(hawkes::spectral_radius({0, 0.36, 0.25, 0}, 2), 0.3, 1e-9);  // sqrt(0.36 * 0.25)
    EXPECT_NEAR(hawkes::spectral_radius({0.5}, 1), 0.5, 1e-12);
    EXPECT_NEAR(hawkes::spectral_radius({0, 0, 0, 0}, 2), 0, 1e-12);
}

// With one type the likelihood is the univariate one of microstructure.hpp (alpha there = a * beta).
TEST(HawkesMv, OneTypeLikelihoodIsTheUnivariateOne) {
    const auto m = model({0.7}, {0.4}, 3.0);
    const hawkes::Realization x = hawkes::simulate(m, 500, 9);
    ASSERT_GT(x.t.size(), 100u);
    EXPECT_NEAR(hawkes::loglik({x}, m), micro::hawkes_loglik(x.t, 500, 0.7, 0.4 * 3.0, 3.0), 1e-6);
}

TEST(HawkesMv, EmRecoversASimulatedTwoTypeProcess) {
    const auto truth = model({0.5, 0.3}, {0.3, 0.2, 0.1, 0.4}, 5.0);
    const auto x = hawkes::simulate(truth, 20000, 11);
    const hawkes::Model f = hawkes::fit({x}, 2);
    ASSERT_TRUE(f.ok);
    for (std::size_t k = 0; k < 4; ++k) EXPECT_NEAR(f.alpha[k], truth.alpha[k], 0.05) << k;
    EXPECT_NEAR(f.beta, 5.0, 1.0);
    EXPECT_NEAR(f.mu[0], 0.5, 0.1);
    EXPECT_NEAR(f.mu[1], 0.3, 0.08);
    EXPECT_NEAR(hawkes::radius(f), hawkes::radius(truth), 0.05);
    // The fitted likelihood is at least the truth's: EM climbs it.
    EXPECT_GE(f.loglik, hawkes::loglik({x}, truth) - 1e-6);
}

TEST(HawkesMv, PoissonDataHasNoExcitation) {
    const auto truth = model({1.0, 2.0}, {0, 0, 0, 0}, 1.0);
    const auto x = hawkes::simulate(truth, 5000, 13);
    const hawkes::Model f = hawkes::fit({x}, 2);
    ASSERT_TRUE(f.ok);
    EXPECT_LT(hawkes::radius(f), 0.05);
    EXPECT_NEAR(f.mu[1], 2.0, 0.15);
}

// Time rescaling: the true model's residuals are Exp(1); a Poisson model of the same data is not.
TEST(HawkesMv, TimeRescalingSeparatesTheRightModel) {
    const auto truth = model({0.5, 0.3}, {0.5, 0.2, 0.1, 0.5}, 5.0);
    const auto x = hawkes::simulate(truth, 10000, 17);
    double n0 = 0, n1 = 0;
    for (const auto t : x.type) (t ? n1 : n0) += 1;
    const auto poisson = model({n0 / 10000, n1 / 10000}, {0, 0, 0, 0}, 1.0);
    const double good = hawkes::rescaling_ks({x}, truth), bad = hawkes::rescaling_ks({x}, poisson);
    EXPECT_LT(good, 0.02);
    EXPECT_GT(bad, 2 * good);
}
