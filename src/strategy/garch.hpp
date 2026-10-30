#pragma once

// Volatility forecasts for quoting and their evaluation (docs/METHODS.md "Volatility forecasts"):
// GARCH(1,1) with normal or Student-t innovations (Bollerslev 1986, 1987) by quasi maximum
// likelihood, the HAR model of realized variance (Corsi 2009), the QLIKE loss (Patton 2011) and the
// Diebold-Mariano test (1995) with a Newey-West variance.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "core/optimize.hpp"
#include "strategy/mlofi.hpp"

namespace lobiq::strategy::vol {

// h_{t+1} = omega + alpha r_t^2 + beta h_t; nu = 0 means normal innovations.
struct GarchFit {
    double omega = 0, alpha = 0, beta = 0, nu = 0, loglik = 0;
    bool ok = false;
    double persistence() const { return alpha + beta; }
    double long_run() const { return omega / (1 - alpha - beta); }
    // Steps for a variance shock to halve.
    double half_life() const { return std::log(0.5) / std::log(alpha + beta); }
};

// Conditional variances h_0..h_n: h_0 = h0, h_n the forecast after the last return.
inline std::vector<double> garch_filter(const std::vector<double>& r, double omega, double alpha, double beta, double h0) {
    std::vector<double> h(r.size() + 1);
    h[0] = h0;
    for (std::size_t t = 0; t < r.size(); ++t) h[t + 1] = omega + alpha * r[t] * r[t] + beta * h[t];
    return h;
}

inline double garch_loglik(const std::vector<double>& r, double omega, double alpha, double beta, double nu, double h0) {
    double ll = 0, h = h0;
    const double c = nu > 0 ? std::lgamma((nu + 1) / 2) - std::lgamma(nu / 2) - 0.5 * std::log(std::numbers::pi * (nu - 2)) : 0;
    for (const double x : r) {
        if (nu > 0) ll += c - 0.5 * std::log(h) - (nu + 1) / 2 * std::log1p(x * x / (h * (nu - 2)));
        else ll += -0.5 * (std::log(2 * std::numbers::pi) + std::log(h) + x * x / h);
        h = omega + alpha * x * x + beta * h;
    }
    return ll;
}

inline double sample_variance(const std::vector<double>& r) {
    if (r.size() < 2) return 0;
    double m = 0, v = 0;
    for (const double x : r) m += x;
    m /= static_cast<double>(r.size());
    for (const double x : r) v += (x - m) * (x - m);
    return v / static_cast<double>(r.size() - 1);
}

// Returns are taken as mean zero. Fitted on r / sd, so the optimiser sees unit scale; the variance
// recursion starts at the sample variance. Persistence and alpha's share of it go through logistic
// maps, so alpha, beta >= 0 and alpha + beta < 1 always hold; nu = 2.1 + exp(.) keeps the variance finite.
inline GarchFit fit_garch(const std::vector<double>& r, bool student) {
    GarchFit out;
    const double var = sample_variance(r);
    if (r.size() < 100 || !(var > 0)) return out;
    const double sd = std::sqrt(var);
    std::vector<double> z(r.size());
    for (std::size_t i = 0; i < r.size(); ++i) z[i] = r[i] / sd;
    auto logistic = [](double x) { return 1 / (1 + std::exp(-x)); };
    auto unpack = [&](const std::vector<double>& p, double& omega, double& alpha, double& beta, double& nu) {
        const double pers = 0.9999 * logistic(p[1]), share = logistic(p[2]);
        alpha = pers * share, beta = pers * (1 - share), omega = std::exp(p[0]);
        nu = student ? std::min(2.1 + std::exp(p[3]), 500.0) : 0;
    };
    auto f = [&](const std::vector<double>& p) {
        double omega = 0, alpha = 0, beta = 0, nu = 0;
        unpack(p, omega, alpha, beta, nu);
        const double ll = garch_loglik(z, omega, alpha, beta, nu, 1.0);
        return std::isfinite(ll) ? -ll : 1e300;
    };
    double best = 1e300;
    std::vector<double> bx;
    // Two starting points: the usual persistent fit, and a weak one for nearly constant variance.
    for (const double pers : {0.95, 0.3}) {
        std::vector<double> x0 = {std::log(1 - pers), std::log(pers / (1 - pers)), std::log(0.1 / 0.9)};
        if (student) x0.push_back(std::log(6.0));
        const opt::Result res = opt::nelder_mead(f, x0, 0.5);
        if (res.f < best) best = res.f, bx = res.x;
    }
    if (!(best < 1e299)) return out;
    unpack(bx, out.omega, out.alpha, out.beta, out.nu);
    out.omega *= var;
    // The likelihood of r is that of z less the Jacobian of the scaling.
    out.loglik = -best - static_cast<double>(r.size()) * std::log(sd);
    out.ok = true;
    return out;
}

// E[h_{t+k}] given h_{t+1}: the forecast decays to the long-run variance at the persistence rate.
inline double variance_ahead(const GarchFit& g, double h_next, std::size_t k) {
    const double lr = g.long_run();
    return lr + std::pow(g.persistence(), static_cast<double>(k - 1)) * (h_next - lr);
}

// Sum of the next m variances: the forecast of the variance of an m-step return.
inline double variance_sum(const GarchFit& g, double h_next, std::size_t m) {
    double s = 0;
    for (std::size_t k = 1; k <= m; ++k) s += variance_ahead(g, h_next, k);
    return s;
}

// GARCH(1,1) on mid changes over a fixed grid of dt seconds, for a market maker: rate() is the
// one-step variance forecast per second, in the units of the mid squared.
struct OnlineGarch {
    double omega = 0, alpha = 0, beta = 0, dt_s = 1;
    double h = NAN, anchor = NAN, last = NAN;
    std::uint64_t boundary = 0, step = 0;

    double rate() const { return h / dt_s; }

    // At each grid boundary the change since the previous boundary is the mid last seen before it.
    // After the first boundary crossed, the changes are 0, so k quiet steps fold into one power.
    void update(std::uint64_t now, double mid) {
        if (!std::isfinite(anchor)) {
            h = omega / (1 - alpha - beta), step = static_cast<std::uint64_t>(dt_s * 1e9);
            anchor = last = mid, boundary = now + step;
            return;
        }
        if (now >= boundary) {
            const double r = last - anchor;
            h = omega + alpha * r * r + beta * h;
            const std::uint64_t quiet = (now - boundary) / step;
            if (quiet) {
                const double lr = omega / (1 - beta);
                h = lr + std::pow(beta, static_cast<double>(quiet)) * (h - lr);
            }
            anchor = last, boundary += (quiet + 1) * step;
        }
        last = mid;
    }
};

// HAR: next-period realized variance on its mean over the last 1, w2 and w3 periods.
struct Har {
    std::size_t w2 = 5, w3 = 30;
    std::vector<double> b;  // b0, b1, b2, b3
    bool ok() const { return b.size() == 4; }
    std::vector<double> row(const std::vector<double>& rv, std::size_t t) const {
        auto mean = [&](std::size_t w) {
            double s = 0;
            for (std::size_t i = t + 1 - w; i <= t; ++i) s += rv[i];
            return s / static_cast<double>(w);
        };
        return {rv[t], mean(w2), mean(w3)};
    }
    // Forecast of rv[t + 1] from rv[..t]; needs t + 1 >= w3.
    double forecast(const std::vector<double>& rv, std::size_t t) const { return mlofi::predict(b, row(rv, t)); }
};

// Fitted by least squares on every period with a full history, over one or more series (days).
inline Har fit_har(const std::vector<std::vector<double>>& series, std::size_t w2 = 5, std::size_t w3 = 30) {
    Har h;
    h.w2 = w2, h.w3 = w3;
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    for (const auto& rv : series)
        for (std::size_t t = w3 - 1; t + 1 < rv.size(); ++t) X.push_back(h.row(rv, t)), y.push_back(rv[t + 1]);
    if (X.size() > 10) h.b = mlofi::ridge(X, y, 0.0);
    return h;
}

// Patton's robust QLIKE, log f + rv / f: ranks forecasts like the true variance does even when rv
// is a noisy proxy, and allows rv = 0.
inline double qlike(double rv, double f) { return std::log(f) + rv / f; }

// Diebold-Mariano statistic of the mean loss difference a - b with a Newey-West variance of `lags`
// lags; positive when b has the lower loss. About N(0, 1) when they forecast equally well.
inline double diebold_mariano(const std::vector<double>& a, const std::vector<double>& b, std::size_t lags) {
    const std::size_t n = a.size();
    if (n < 10 || b.size() != n) return NAN;
    std::vector<double> d(n);
    double m = 0;
    for (std::size_t i = 0; i < n; ++i) d[i] = a[i] - b[i], m += d[i];
    m /= static_cast<double>(n);
    auto gamma = [&](std::size_t k) {
        double s = 0;
        for (std::size_t i = k; i < n; ++i) s += (d[i] - m) * (d[i - k] - m);
        return s / static_cast<double>(n);
    };
    double lrv = gamma(0);
    for (std::size_t k = 1; k <= lags && k < n; ++k) lrv += 2 * (1 - static_cast<double>(k) / static_cast<double>(lags + 1)) * gamma(k);
    return lrv > 0 ? m / std::sqrt(lrv / static_cast<double>(n)) : NAN;
}

}  // namespace lobiq::strategy::vol
