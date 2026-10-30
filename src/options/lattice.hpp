#pragma once

// Binomial tree of Cox, Ross and Rubinstein (1979) for European and American options: u = e^{s sqrt(dt)},
// d = 1/u, risk-neutral p = (e^{(r-q) dt} - d) / (u - d). The American value checks early exercise
// at every node. Delta and gamma come from the first two time steps of the tree.

#include <algorithm>
#include <cmath>
#include <vector>

#include "options/black_scholes.hpp"

namespace hft::options {

struct TreeResult {
    double price = 0, delta = 0, gamma = 0;
};

inline TreeResult binomial(Kind kind, bool american, double S, double K, double T, double r, double q, double sigma, int steps = 500) {
    TreeResult out;
    const double dt = T / steps, u = std::exp(sigma * std::sqrt(dt)), d = 1 / u;
    const double disc = std::exp(-r * dt), p = (std::exp((r - q) * dt) - d) / (u - d);
    auto payoff = [&](double s) { return std::max(kind == Kind::Call ? s - K : K - s, 0.0); };
    std::vector<double> v(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) v[static_cast<std::size_t>(i)] = payoff(S * std::pow(u, 2 * i - steps));
    double v1[2] = {0, 0}, v2[3] = {0, 0, 0};
    for (int n = steps - 1; n >= 0; --n) {
        for (int i = 0; i <= n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            double x = disc * (p * v[k + 1] + (1 - p) * v[k]);
            if (american) x = std::max(x, payoff(S * std::pow(u, 2 * i - n)));
            v[k] = x;
        }
        if (n == 2) v2[0] = v[0], v2[1] = v[1], v2[2] = v[2];
        if (n == 1) v1[0] = v[0], v1[1] = v[1];
    }
    out.price = v[0];
    if (steps >= 2) {
        out.delta = (v1[1] - v1[0]) / (S * u - S * d);
        const double up = (v2[2] - v2[1]) / (S * u * u - S), down = (v2[1] - v2[0]) / (S - S * d * d);
        out.gamma = (up - down) / (0.5 * (S * u * u - S * d * d));
    }
    return out;
}

}  // namespace hft::options
