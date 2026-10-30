#pragma once

// Multivariate Hawkes process with exponential kernels sharing one decay (docs/METHODS.md
// "Multivariate Hawkes"): lambda_i(t) = mu_i + sum_j a_ij sum_{t_k < t, d_k = j} beta exp(-beta (t - t_k)),
// so a_ij is the expected number of type-i events one type-j event triggers. Fitted by EM (Lewis and
// Mohler 2011) for each decay on a grid, then the decay refined by golden-section search on the
// likelihood; simulated by Ogata's thinning; checked by time rescaling (Brown et al. 2002).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/philox.hpp"

namespace hft::strategy::hawkes {

// Events on [0, horizon] in seconds, sorted by time; type[k] in [0, D).
struct Realization {
    std::vector<double> t;
    std::vector<std::uint8_t> type;
    double horizon = 0;
};

struct Model {
    std::size_t D = 0;
    std::vector<double> mu;     // 1/s
    std::vector<double> alpha;  // D x D row-major: alpha[i * D + j], triggered i per j
    double beta = 1;            // 1/s
    double loglik = 0;
    bool ok = false;
    double a(std::size_t i, std::size_t j) const { return alpha[i * D + j]; }
};

// Perron root of a non-negative matrix by power iteration: stationary when below 1, and then the
// share of events that were triggered by earlier ones.
inline double spectral_radius(const std::vector<double>& m, std::size_t D) {
    // Iterates on I + m, which has the same Perron vector and no period, so the ratio converges.
    std::vector<double> v(D, 1.0), w(D);  // v sums to D
    double r = 0;
    for (int it = 0; it < 2000; ++it) {
        double sum = 0;
        for (std::size_t i = 0; i < D; ++i) {
            w[i] = v[i];
            for (std::size_t j = 0; j < D; ++j) w[i] += m[i * D + j] * v[j];
            sum += w[i];
        }
        const double next = sum / static_cast<double>(D);
        for (std::size_t i = 0; i < D; ++i) v[i] = w[i] / next;
        if (std::abs(next - r) < 1e-13 * next) return next - 1;
        r = next;
    }
    return r - 1;
}

inline double radius(const Model& m) { return spectral_radius(m.alpha, m.D); }

// Log-likelihood of the realizations under m.
inline double loglik(const std::vector<Realization>& data, const Model& m) {
    const std::size_t D = m.D;
    double ll = 0;
    std::vector<double> R(D);
    for (const Realization& x : data) {
        std::fill(R.begin(), R.end(), 0.0);
        double prev = 0;
        for (std::size_t k = 0; k < x.t.size(); ++k) {
            const double decay = std::exp(-m.beta * (x.t[k] - prev));
            for (double& r : R) r *= decay;
            const std::size_t i = x.type[k];
            double lam = m.mu[i];
            for (std::size_t j = 0; j < D; ++j) lam += m.alpha[i * D + j] * R[j];
            ll += std::log(lam);
            R[i] += m.beta;
            prev = x.t[k];
            double out = 0;
            for (std::size_t r = 0; r < D; ++r) out += m.alpha[r * D + i];
            ll -= out * -std::expm1(-m.beta * (x.horizon - x.t[k]));
        }
        for (std::size_t i = 0; i < D; ++i) ll -= m.mu[i] * x.horizon;
    }
    return ll;
}

// EM at a fixed decay. Each pass computes, from the current parameters, the expected number of
// background and of j-triggered type-i events; the M-step divides them by the exposure.
inline Model fit_em(const std::vector<Realization>& data, std::size_t D, double beta, int max_iter = 300, double tol = 1e-9) {
    Model m;
    m.D = D, m.beta = beta;
    double T = 0;
    std::vector<double> n(D, 0), G(D, 0);  // events per type; sum over type-j events of 1 - exp(-beta (T - t_k))
    for (const Realization& x : data) {
        T += x.horizon;
        for (std::size_t k = 0; k < x.t.size(); ++k) n[x.type[k]] += 1, G[x.type[k]] += -std::expm1(-beta * (x.horizon - x.t[k]));
    }
    double total = 0;
    for (const double c : n) total += c;
    if (T <= 0 || total < 2 * static_cast<double>(D)) return m;
    m.mu.resize(D), m.alpha.assign(D * D, 0.1 / static_cast<double>(D));
    for (std::size_t i = 0; i < D; ++i) m.mu[i] = std::max(0.5 * n[i] / T, 1e-6 / T);
    std::vector<double> bg(D), trig(D * D), R(D);
    double prev_ll = -1e300;
    for (int it = 0; it < max_iter; ++it) {
        std::fill(bg.begin(), bg.end(), 0.0);
        std::fill(trig.begin(), trig.end(), 0.0);
        double ll = 0;
        for (const Realization& x : data) {
            std::fill(R.begin(), R.end(), 0.0);
            double prev = 0;
            for (std::size_t k = 0; k < x.t.size(); ++k) {
                const double decay = std::exp(-beta * (x.t[k] - prev));
                for (double& r : R) r *= decay;
                const std::size_t i = x.type[k];
                double lam = m.mu[i];
                for (std::size_t j = 0; j < D; ++j) lam += m.alpha[i * D + j] * R[j];
                ll += std::log(lam);
                bg[i] += m.mu[i] / lam;
                for (std::size_t j = 0; j < D; ++j) trig[i * D + j] += m.alpha[i * D + j] * R[j] / lam;
                R[i] += beta;
                prev = x.t[k];
            }
        }
        for (std::size_t i = 0; i < D; ++i) {
            ll -= m.mu[i] * T;
            for (std::size_t j = 0; j < D; ++j) ll -= m.alpha[i * D + j] * G[j];
        }
        m.loglik = ll;
        for (std::size_t i = 0; i < D; ++i) {
            m.mu[i] = std::max(bg[i] / T, 1e-12);
            for (std::size_t j = 0; j < D; ++j) m.alpha[i * D + j] = G[j] > 0 ? trig[i * D + j] / G[j] : 0;
        }
        if (std::abs(ll - prev_ll) < tol * (1 + std::abs(ll))) break;
        prev_ll = ll;
    }
    m.loglik = loglik(data, m);
    m.ok = std::isfinite(m.loglik);
    return m;
}

// The decay with the best EM likelihood on a log grid, then golden-section between its neighbours.
inline Model fit(const std::vector<Realization>& data, std::size_t D,
                 const std::vector<double>& grid = {0.03, 0.1, 0.3, 1, 3, 10, 30, 100, 300, 1000}) {
    Model best;
    std::size_t at = 0;
    for (std::size_t g = 0; g < grid.size(); ++g) {
        Model m = fit_em(data, D, grid[g]);
        if (m.ok && (!best.ok || m.loglik > best.loglik)) best = std::move(m), at = g;
    }
    if (!best.ok) return best;
    double lo = std::log(grid[at > 0 ? at - 1 : at]), hi = std::log(grid[at + 1 < grid.size() ? at + 1 : at]);
    constexpr double phi = 0.6180339887498949;
    auto at_log = [&](double lb) { return fit_em(data, D, std::exp(lb)); };
    double x1 = hi - phi * (hi - lo), x2 = lo + phi * (hi - lo);
    Model m1 = at_log(x1), m2 = at_log(x2);
    for (int it = 0; it < 12 && hi - lo > 1e-3; ++it) {
        if (m1.loglik > m2.loglik) hi = x2, x2 = x1, m2 = std::move(m1), x1 = hi - phi * (hi - lo), m1 = at_log(x1);
        else lo = x1, x1 = x2, m1 = std::move(m2), x2 = lo + phi * (hi - lo), m2 = at_log(x2);
        for (Model* c : {&m1, &m2})
            if (c->ok && c->loglik > best.loglik) best = *c;
    }
    return best;
}

// Ogata's thinning on [0, horizon]. Between events every intensity decays, so the total intensity
// just after the last event bounds it until the next candidate.
inline Realization simulate(const Model& m, double horizon, std::uint64_t seed) {
    Realization x;
    x.horizon = horizon;
    const std::size_t D = m.D;
    const rng::Stream rng(seed, 0, 0);
    std::vector<double> R(D, 0.0), lam(D);
    double now = 0;
    for (std::uint32_t draw = 0;; ++draw) {
        double bound = 0;
        for (std::size_t i = 0; i < D; ++i) {
            lam[i] = m.mu[i];
            for (std::size_t j = 0; j < D; ++j) lam[i] += m.alpha[i * D + j] * R[j];
            bound += lam[i];
        }
        const double step = -std::log(rng.uniform(draw, 0)) / bound;
        now += step;
        if (now >= horizon) return x;
        const double decay = std::exp(-m.beta * step);
        for (double& r : R) r *= decay;
        double total = 0;
        for (std::size_t i = 0; i < D; ++i) {
            lam[i] = m.mu[i];
            for (std::size_t j = 0; j < D; ++j) lam[i] += m.alpha[i * D + j] * R[j];
            total += lam[i];
        }
        double u = rng.uniform(draw, 1) * bound;
        if (u > total) continue;
        std::size_t i = 0;
        while (i + 1 < D && u > lam[i]) u -= lam[i++];
        x.t.push_back(now), x.type.push_back(static_cast<std::uint8_t>(i));
        R[i] += m.beta;
    }
}

// Time rescaling: the compensator of each type between its consecutive events is Exp(1) under a
// correct model. Returns the Kolmogorov-Smirnov distance of all those increments from Exp(1).
inline double rescaling_ks(const std::vector<Realization>& data, const Model& m) {
    const std::size_t D = m.D;
    std::vector<double> z;
    for (const Realization& x : data) {
        std::vector<double> R(D, 0.0), acc(D, 0.0);
        std::vector<bool> seen(D, false);
        double prev = 0;
        for (std::size_t k = 0; k < x.t.size(); ++k) {
            const double dt = x.t[k] - prev, e = -std::expm1(-m.beta * dt);
            for (std::size_t i = 0; i < D; ++i) {
                double c = m.mu[i] * dt;
                for (std::size_t j = 0; j < D; ++j) c += m.alpha[i * D + j] * R[j] / m.beta * e;
                acc[i] += c;
            }
            for (double& r : R) r *= 1 - e;
            const std::size_t i = x.type[k];
            if (seen[i]) z.push_back(acc[i]);
            seen[i] = true, acc[i] = 0;
            R[i] += m.beta;
            prev = x.t[k];
        }
    }
    if (z.empty()) return NAN;
    std::sort(z.begin(), z.end());
    double ks = 0;
    const double n = static_cast<double>(z.size());
    for (std::size_t k = 0; k < z.size(); ++k) {
        const double F = -std::expm1(-z[k]);
        ks = std::max({ks, std::abs(F - static_cast<double>(k) / n), std::abs(static_cast<double>(k + 1) / n - F)});
    }
    return ks;
}

}  // namespace hft::strategy::hawkes
