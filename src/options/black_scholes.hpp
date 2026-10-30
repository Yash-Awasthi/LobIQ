#pragma once

// European options under Black-Scholes-Merton with a continuous dividend yield q (docs/METHODS.md
// "Options"): prices, Greeks, cash-or-nothing digitals and implied volatility. Time in years, rates
// continuously compounded, theta per year.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace lobiq::options {

enum class Kind { Call, Put };

inline double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::numbers::sqrt2); }
inline double norm_pdf(double x) { return std::exp(-0.5 * x * x) / std::sqrt(2 * std::numbers::pi); }

struct Greeks {
    double price = 0, delta = 0, gamma = 0, vega = 0, theta = 0, rho = 0, vanna = 0, volga = 0;
};

inline Greeks black_scholes(Kind kind, double S, double K, double T, double r, double q, double sigma) {
    Greeks g;
    const double dq = std::exp(-q * T), dr = std::exp(-r * T);
    if (T <= 0 || sigma <= 0) {  // at expiry, or no volatility: the discounted forward intrinsic value
        const double fwd = S * dq - K * dr;
        const bool itm = kind == Kind::Call ? fwd > 0 : fwd < 0;
        g.price = itm ? std::abs(fwd) : 0;
        g.delta = itm ? (kind == Kind::Call ? dq : -dq) : 0;
        return g;
    }
    const double sq = sigma * std::sqrt(T);
    const double d1 = (std::log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / sq, d2 = d1 - sq;
    const double pdf = norm_pdf(d1);
    g.gamma = dq * pdf / (S * sq);
    g.vega = S * dq * pdf * std::sqrt(T);
    g.vanna = -dq * pdf * d2 / sigma;
    g.volga = g.vega * d1 * d2 / sigma;
    const double decay = -S * dq * pdf * sigma / (2 * std::sqrt(T));
    if (kind == Kind::Call) {
        g.price = S * dq * norm_cdf(d1) - K * dr * norm_cdf(d2);
        g.delta = dq * norm_cdf(d1);
        g.theta = decay - r * K * dr * norm_cdf(d2) + q * S * dq * norm_cdf(d1);
        g.rho = K * T * dr * norm_cdf(d2);
    } else {
        g.price = K * dr * norm_cdf(-d2) - S * dq * norm_cdf(-d1);
        g.delta = -dq * norm_cdf(-d1);
        g.theta = decay + r * K * dr * norm_cdf(-d2) - q * S * dq * norm_cdf(-d1);
        g.rho = -K * T * dr * norm_cdf(-d2);
    }
    return g;
}

inline double bs_price(Kind kind, double S, double K, double T, double r, double q, double sigma) {
    return black_scholes(kind, S, K, T, r, q, sigma).price;
}

// Pays 1 at T if S_T ends above K (call) or below (put): e^{-rT} N(+-d2). A prediction-market
// share on "price above K" is this call. Theta and rho by central differences.
inline Greeks digital(Kind kind, double S, double K, double T, double r, double q, double sigma) {
    Greeks g;
    if (T <= 0 || sigma <= 0) {
        const bool up = S * std::exp(-q * T) > K * std::exp(-r * T);
        g.price = (kind == Kind::Call) == up ? std::exp(-r * std::max(T, 0.0)) : 0;
        return g;
    }
    const double sq = sigma * std::sqrt(T), dr = std::exp(-r * T);
    const double d1 = (std::log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / sq, d2 = d1 - sq;
    const double sign = kind == Kind::Call ? 1 : -1, pdf = norm_pdf(d2);
    g.price = dr * norm_cdf(sign * d2);
    g.delta = sign * dr * pdf / (S * sq);
    g.gamma = -sign * dr * pdf * d1 / (S * S * sq * sq);
    g.vega = -sign * dr * pdf * d1 / sigma;
    const double h = std::min(1e-4, T / 2);
    auto price_at = [&](double t, double rr) {
        const double s = sigma * std::sqrt(t);
        return std::exp(-rr * t) * norm_cdf(sign * ((std::log(S / K) + (rr - q - 0.5 * sigma * sigma) * t) / s));
    };
    g.theta = -(price_at(T + h, r) - price_at(T - h, r)) / (2 * h);
    g.rho = (price_at(T, r + 1e-4) - price_at(T, r - 1e-4)) / 2e-4;
    return g;
}

// The volatility that reproduces a European price: Newton steps on vega inside a bisection bracket,
// so it cannot diverge. NaN when the price is outside the no-arbitrage bounds.
inline double implied_vol(Kind kind, double price, double S, double K, double T, double r, double q) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!(T > 0) || !(price > 0)) return nan;
    const double fs = S * std::exp(-q * T), fk = K * std::exp(-r * T);
    const double lower = kind == Kind::Call ? std::max(fs - fk, 0.0) : std::max(fk - fs, 0.0);
    const double upper = kind == Kind::Call ? fs : fk;
    if (price <= lower || price >= upper) return nan;
    double lo = 1e-6, hi = 10;
    double x = std::clamp(std::sqrt(2 * std::numbers::pi / T) * price / fs, 0.01, 3.0);  // Brenner-Subrahmanyam
    for (int it = 0; it < 100; ++it) {
        const Greeks g = black_scholes(kind, S, K, T, r, q, x);
        const double diff = g.price - price;
        if (std::abs(diff) < 1e-12 * std::max(1.0, price)) return x;
        (diff > 0 ? hi : lo) = x;
        const double next = g.vega > 1e-14 ? x - diff / g.vega : NAN;
        x = std::isfinite(next) && next > lo && next < hi ? next : 0.5 * (lo + hi);
        if (hi - lo < 1e-14) break;
    }
    return x;
}

}  // namespace lobiq::options
