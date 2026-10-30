#pragma once

// Monte Carlo under geometric Brownian motion. By Ito's lemma, d log S = (mu - sigma^2 / 2) dt + sigma dW,
// so S_T = S exp((mu - sigma^2 / 2) T + sigma sqrt(T) Z) exactly, with no discretisation error.
// European prices use antithetic pairs and S_T as a control variate (E[S_T] = S e^{(r-q)T} is
// known); the pathwise delta is e^{-rT} 1{in the money} S_T / S for a call.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include "core/philox.hpp"
#include "options/black_scholes.hpp"

namespace lobiq::options {

// Standard normal pair from one philox draw by Box-Muller.
inline std::pair<double, double> normal_pair(const rng::Stream& rng, std::uint32_t i) {
    const auto r = rng.draw(i, 7);
    const double u1 = (r[0] + 0.5) * 0x1p-32, u2 = (r[1] + 0.5) * 0x1p-32;
    const double m = std::sqrt(-2 * std::log(u1));
    return {m * std::cos(2 * std::numbers::pi * u2), m * std::sin(2 * std::numbers::pi * u2)};
}

inline double gbm_terminal(double S, double T, double mu, double sigma, double z) {
    return S * std::exp((mu - 0.5 * sigma * sigma) * T + sigma * std::sqrt(T) * z);
}

struct McResult {
    double price = 0, std_error = 0, delta = 0;
    std::size_t paths = 0;
};

inline McResult monte_carlo(Kind kind, double S, double K, double T, double r, double q, double sigma, std::size_t pairs = 200000,
                            std::uint64_t seed = 1) {
    McResult out;
    const rng::Stream rng(seed, 0, 0);
    const double disc = std::exp(-r * T), fwd = S * std::exp((r - q) * T);
    std::vector<double> y(pairs), c(pairs);
    double dsum = 0;
    for (std::size_t i = 0; i < pairs; ++i) {
        const double z = normal_pair(rng, static_cast<std::uint32_t>(i)).first;
        double pay = 0, st = 0;
        for (const double s : {z, -z}) {
            const double x = gbm_terminal(S, T, r - q, sigma, s);
            const bool itm = kind == Kind::Call ? x > K : x < K;
            pay += itm ? std::abs(x - K) : 0;
            st += x;
            if (itm) dsum += (kind == Kind::Call ? 1 : -1) * x / S;
        }
        y[i] = disc * pay / 2, c[i] = st / 2;
    }
    // Control variate: y - b (c - E c) with the variance-minimising b.
    const double n = static_cast<double>(pairs);
    double my = 0, mc = 0;
    for (std::size_t i = 0; i < pairs; ++i) my += y[i] / n, mc += c[i] / n;
    double cov = 0, var = 0;
    for (std::size_t i = 0; i < pairs; ++i) cov += (y[i] - my) * (c[i] - mc), var += (c[i] - mc) * (c[i] - mc);
    const double b = var > 0 ? cov / var : 0;
    double m = 0, s2 = 0;
    for (std::size_t i = 0; i < pairs; ++i) {
        const double a = y[i] - b * (c[i] - fwd);
        m += a / n;
        s2 += a * a / n;
    }
    out.price = m;
    out.std_error = std::sqrt(std::max(s2 - m * m, 0.0) / (n - 1));
    out.delta = disc * dsum / (2 * n);
    out.paths = 2 * pairs;
    return out;
}

}  // namespace lobiq::options
