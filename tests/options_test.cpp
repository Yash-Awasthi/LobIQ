#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "options/black_scholes.hpp"
#include "options/lattice.hpp"
#include "options/monte_carlo.hpp"
#include "options/svi.hpp"

using namespace hft;
using namespace hft::options;

TEST(BlackScholes, PutCallParity) {
    for (const double K : {80.0, 100.0, 125.0}) {
        const double c = bs_price(Kind::Call, 100, K, 0.7, 0.03, 0.01, 0.25), p = bs_price(Kind::Put, 100, K, 0.7, 0.03, 0.01, 0.25);
        EXPECT_NEAR(c - p, 100 * std::exp(-0.01 * 0.7) - K * std::exp(-0.03 * 0.7), 1e-10);
    }
}

TEST(BlackScholes, TextbookValue) {
    // Hull: S = 42, K = 40, r = 10%, sigma = 20%, T = 0.5 gives a call of 4.76 and a put of 0.81.
    EXPECT_NEAR(bs_price(Kind::Call, 42, 40, 0.5, 0.1, 0, 0.2), 4.7594, 1e-4);
    EXPECT_NEAR(bs_price(Kind::Put, 42, 40, 0.5, 0.1, 0, 0.2), 0.8086, 1e-4);
}

// Every analytic Greek against a central difference of the price.
TEST(BlackScholes, GreeksMatchFiniteDifferences) {
    for (const Kind k : {Kind::Call, Kind::Put}) {
        const double S = 100, K = 95, T = 0.4, r = 0.02, q = 0.015, v = 0.3, h = 1e-3, hv = 1e-4;
        const Greeks g = black_scholes(k, S, K, T, r, q, v);
        auto P = [&](double s, double t, double rr, double vv) { return bs_price(k, s, K, t, rr, q, vv); };
        EXPECT_NEAR(g.delta, (P(S + h, T, r, v) - P(S - h, T, r, v)) / (2 * h), 1e-6);
        EXPECT_NEAR(g.gamma, (P(S + h, T, r, v) - 2 * g.price + P(S - h, T, r, v)) / (h * h), 1e-4);
        EXPECT_NEAR(g.vega, (P(S, T, r, v + hv) - P(S, T, r, v - hv)) / (2 * hv), 1e-6);
        EXPECT_NEAR(g.theta, -(P(S, T + h, r, v) - P(S, T - h, r, v)) / (2 * h), 1e-4);
        EXPECT_NEAR(g.rho, (P(S, T, r + h, v) - P(S, T, r - h, v)) / (2 * h), 1e-4);
        const double dv = (black_scholes(k, S + h, K, T, r, q, v).vega - black_scholes(k, S - h, K, T, r, q, v).vega) / (2 * h);
        EXPECT_NEAR(g.vanna, dv, 1e-5);
        const double dvv = (black_scholes(k, S, K, T, r, q, v + hv).vega - black_scholes(k, S, K, T, r, q, v - hv).vega) / (2 * hv);
        EXPECT_NEAR(g.volga, dvv, 1e-4);
    }
}

TEST(ImpliedVol, RoundTripsAndRejectsArbitrage) {
    for (const Kind k : {Kind::Call, Kind::Put})
        for (const double K : {50.0, 90.0, 100.0, 110.0, 200.0})
            for (const double v : {0.05, 0.2, 0.8, 2.0}) {
                const double p = bs_price(k, 100, K, 0.5, 0.03, 0.01, v);
                const double fwd = 100 * std::exp(-0.01 * 0.5) - K * std::exp(-0.03 * 0.5);
                if (p - std::max(k == Kind::Call ? fwd : -fwd, 0.0) < 1e-6) continue;  // no time value left to invert
                EXPECT_NEAR(implied_vol(k, p, 100, K, 0.5, 0.03, 0.01), v, 1e-6) << K << " " << v;
            }
    EXPECT_TRUE(std::isnan(implied_vol(Kind::Call, 0.5, 100, 50, 0.5, 0, 0)));   // below intrinsic
    EXPECT_TRUE(std::isnan(implied_vol(Kind::Call, 101, 100, 50, 0.5, 0, 0)));   // above the stock
}

// A digital call is minus the strike derivative of the call; a digital call and put pay 1 together.
TEST(Digital, IsTheStrikeDerivativeOfTheCall) {
    const double S = 100, T = 0.3, r = 0.04, q = 0.0, v = 0.35, h = 1e-3;
    for (const double K : {80.0, 100.0, 120.0}) {
        const Greeks d = digital(Kind::Call, S, K, T, r, q, v);
        EXPECT_NEAR(d.price, -(bs_price(Kind::Call, S, K + h, T, r, q, v) - bs_price(Kind::Call, S, K - h, T, r, q, v)) / (2 * h), 1e-6);
        EXPECT_NEAR(d.price + digital(Kind::Put, S, K, T, r, q, v).price, std::exp(-r * T), 1e-12);
        auto P = [&](double s, double vv) { return digital(Kind::Call, s, K, T, r, q, vv).price; };
        EXPECT_NEAR(d.delta, (P(S + h, v) - P(S - h, v)) / (2 * h), 1e-7);
        EXPECT_NEAR(d.gamma, (P(S + h, v) - 2 * d.price + P(S - h, v)) / (h * h), 1e-5);
        EXPECT_NEAR(d.vega, (P(S, v + 1e-4) - P(S, v - 1e-4)) / 2e-4, 1e-7);
    }
}

TEST(Binomial, ConvergesToBlackScholesAndValuesEarlyExercise) {
    const double S = 100, K = 105, T = 1, r = 0.05, v = 0.25;
    const TreeResult eu = binomial(Kind::Put, false, S, K, T, r, 0, v, 2000), am = binomial(Kind::Put, true, S, K, T, r, 0, v, 2000);
    const Greeks bs = black_scholes(Kind::Put, S, K, T, r, 0, v);
    EXPECT_NEAR(eu.price, bs.price, 5e-3);
    EXPECT_NEAR(eu.delta, bs.delta, 2e-3);
    EXPECT_NEAR(eu.gamma, bs.gamma, 1e-3);
    EXPECT_GT(am.price, eu.price + 0.1);
    // Without dividends an American call is never exercised early.
    EXPECT_NEAR(binomial(Kind::Call, true, S, K, T, r, 0, v, 1000).price, binomial(Kind::Call, false, S, K, T, r, 0, v, 1000).price, 1e-10);
}

TEST(MonteCarlo, AgreesWithBlackScholesWithinItsError) {
    for (const Kind k : {Kind::Call, Kind::Put}) {
        const McResult mc = monte_carlo(k, 100, 110, 0.75, 0.03, 0.01, 0.3, 100000, 5);
        const Greeks bs = black_scholes(k, 100, 110, 0.75, 0.03, 0.01, 0.3);
        EXPECT_NEAR(mc.price, bs.price, 4 * mc.std_error);
        EXPECT_LT(mc.std_error, 0.02);
        EXPECT_NEAR(mc.delta, bs.delta, 0.01);
    }
}

// Ito's lemma: log S_T has mean log S + (mu - sigma^2 / 2) T while S_T has mean S e^{mu T}.
TEST(MonteCarlo, ItoDriftCorrection) {
    const rng::Stream r(9, 0, 0);
    const double S = 100, T = 2, mu = 0.05, sigma = 0.4;
    double logs = 0, level = 0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        const double x = gbm_terminal(S, T, mu, sigma, normal_pair(r, static_cast<std::uint32_t>(i)).first);
        logs += std::log(x) / n, level += x / n;
    }
    EXPECT_NEAR(logs, std::log(S) + (mu - 0.5 * sigma * sigma) * T, 0.005);
    EXPECT_NEAR(level / (S * std::exp(mu * T)), 1, 0.01);
}

TEST(Svi, FitRecoversASliceAndItsDensityIsPositive) {
    Svi truth;
    truth.a = 0.01, truth.b = 0.06, truth.rho = -0.5, truth.m = 0.03, truth.s = 0.12, truth.T = 0.5;
    std::vector<double> k, vol, w;
    for (int i = -12; i <= 12; ++i) k.push_back(0.03 * i), vol.push_back(truth.vol(0.03 * i)), w.push_back(1);
    const Svi f = fit_svi(k, vol, w, 0.5);
    ASSERT_TRUE(f.ok);
    EXPECT_LT(f.rmse_vol, 1e-4);
    for (const double x : k) EXPECT_NEAR(f.vol(x), truth.vol(x), 2e-4);
    EXPECT_GT(min_density(truth, -0.4, 0.4), 0);
}

TEST(Svi, CalendarCheckFlagsFallingTotalVariance) {
    Svi a, b;
    a.a = 0.01, a.b = 0.05, a.rho = -0.4, a.s = 0.1, a.T = 0.25;
    b = a;
    b.a = 0.02, b.T = 0.5;
    Surface ok{{a, b}};
    EXPECT_EQ(ok.calendar_violations(-0.3, 0.3), 0u);
    EXPECT_NEAR(ok.w(0.1, 0.375), 0.5 * (a.w(0.1) + b.w(0.1)), 1e-12);
    std::swap(a.T, b.T);
    Surface bad{{b, a}};
    EXPECT_GT(bad.calendar_violations(-0.3, 0.3), 0u);
}
