#pragma once

// Implied volatility surface from SVI slices (Gatheral 2004; Gatheral and Jacquier 2014): per
// expiry the total implied variance w(k) = sigma^2 T at log-moneyness k = log(K / F) is
// a + b (rho (k - m) + sqrt((k - m)^2 + s^2)); between expiries w is linear in T at fixed k. Checks
// for butterfly arbitrage (a negative implied density) and calendar arbitrage (w falling with T).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/optimize.hpp"

namespace hft::options {

struct Svi {
    double a = 0, b = 0, rho = 0, m = 0, s = 0.1;
    double T = 0, forward = 0;
    double rmse_vol = 0;  // fit error in implied volatility
    bool ok = false;
    double w(double k) const { return a + b * (rho * (k - m) + std::sqrt((k - m) * (k - m) + s * s)); }
    double w1(double k) const { return b * (rho + (k - m) / std::sqrt((k - m) * (k - m) + s * s)); }
    double w2(double k) const {
        const double r = (k - m) * (k - m) + s * s;
        return b * s * s / (r * std::sqrt(r));
    }
    double vol(double k) const { return std::sqrt(std::max(w(k), 0.0) / T); }
    // Durrleman's condition: the risk-neutral density is g(k) n(d2) / sqrt(w), so g < 0 is arbitrage.
    double g(double k) const {
        const double x = w(k), d = w1(k);
        return (1 - k * d / (2 * x)) * (1 - k * d / (2 * x)) - d * d / 4 * (1 / x + 0.25) + w2(k) / 2;
    }
};

// Least squares in total variance, weighted by `weight` (e.g. vega), over b > 0, |rho| < 1, s > 0
// and a + b s sqrt(1 - rho^2) >= 0 (the smallest total variance is not negative).
inline Svi fit_svi(const std::vector<double>& k, const std::vector<double>& vol, const std::vector<double>& weight, double T) {
    Svi best;
    best.T = T;
    const std::size_t n = k.size();
    if (n < 5 || T <= 0) return best;
    std::vector<double> w(n);
    double wmin = 1e300;
    for (std::size_t i = 0; i < n; ++i) w[i] = vol[i] * vol[i] * T, wmin = std::min(wmin, w[i]);
    auto unpack = [&](const std::vector<double>& p) {
        Svi x;
        x.T = T, x.a = p[0], x.b = std::exp(p[1]), x.rho = std::tanh(p[2]), x.m = p[3], x.s = std::exp(p[4]);
        return x;
    };
    auto f = [&](const std::vector<double>& p) {
        const Svi x = unpack(p);
        const double floor = x.a + x.b * x.s * std::sqrt(1 - x.rho * x.rho);
        double e = floor < 0 ? 1e3 * floor * floor : 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double d = x.w(k[i]) - w[i];
            e += weight[i] * d * d;
        }
        return std::isfinite(e) ? e : 1e300;
    };
    double bestf = 1e300;
    for (const double rho0 : {-0.5, 0.0, 0.5})
        for (const double m0 : {-0.1, 0.0, 0.1}) {
            const std::vector<double> x0 = {0.5 * wmin, std::log(0.1), std::atanh(rho0), m0, std::log(0.1)};
            opt::Result r = opt::nelder_mead(f, x0, 0.3, 6000, 1e-14);
            r = opt::nelder_mead(f, r.x, 0.05, 6000, 1e-14);  // restart: Nelder-Mead can stall
            if (r.f < bestf) bestf = r.f, best = unpack(r.x);
        }
    best.T = T;
    double se = 0;
    for (std::size_t i = 0; i < n; ++i) se += (best.vol(k[i]) - vol[i]) * (best.vol(k[i]) - vol[i]);
    best.rmse_vol = std::sqrt(se / static_cast<double>(n));
    best.ok = bestf < 1e299;
    return best;
}

// Smallest g on a grid of k: negative means butterfly arbitrage.
inline double min_density(const Svi& x, double kmin, double kmax, int points = 201) {
    double lo = 1e300;
    for (int i = 0; i < points; ++i) lo = std::min(lo, x.g(kmin + (kmax - kmin) * i / (points - 1)));
    return lo;
}

// Slices sorted by expiry.
struct Surface {
    std::vector<Svi> slices;

    // Total variance at (k, T): linear in T between slices; before the first slice it scales with
    // T at the first slice's volatility, after the last it keeps the last slice's volatility.
    double w(double k, double T) const {
        if (slices.empty()) return NAN;
        if (T <= slices.front().T) return slices.front().w(k) * T / slices.front().T;
        if (T >= slices.back().T) return slices.back().w(k) * T / slices.back().T;
        std::size_t i = 1;
        while (slices[i].T < T) ++i;
        const Svi &a = slices[i - 1], &b = slices[i];
        const double x = (T - a.T) / (b.T - a.T);
        return a.w(k) + x * (b.w(k) - a.w(k));
    }
    double vol(double k, double T) const { return std::sqrt(std::max(w(k, T), 0.0) / T); }

    // Grid points in [kmin, kmax] where a later slice has less total variance than an earlier one.
    std::size_t calendar_violations(double kmin, double kmax, int points = 101) const {
        std::size_t bad = 0;
        for (std::size_t i = 1; i < slices.size(); ++i)
            for (int j = 0; j < points; ++j) {
                const double k = kmin + (kmax - kmin) * j / (points - 1);
                bad += slices[i].w(k) < slices[i - 1].w(k) - 1e-12;
            }
        return bad;
    }
};

}  // namespace hft::options
