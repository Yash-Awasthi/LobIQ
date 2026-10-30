#pragma once

// Market regimes from per-period features (docs/METHODS.md "Regimes"): k-means++ (Arthur and
// Vassilvitskii 2007) to start a Gaussian hidden Markov model with diagonal covariances, fitted by
// Baum-Welch (Rabiner 1989) over several sequences. Regimes are read causally by the forward filter,
// so a label at period t uses nothing after t; Viterbi gives the smoothed path for description.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "core/philox.hpp"

namespace lobiq::strategy::regime {

using Row = std::vector<double>;
using Seq = std::vector<Row>;

// Per-feature mean and standard deviation from training rows, applied unchanged to later ones.
struct Standardizer {
    std::vector<double> mean, sd;
    void fit(const std::vector<const Row*>& rows) {
        const std::size_t F = rows.empty() ? 0 : rows[0]->size();
        mean.assign(F, 0), sd.assign(F, 0);
        const double n = static_cast<double>(rows.size());
        for (const Row* r : rows)
            for (std::size_t f = 0; f < F; ++f) mean[f] += (*r)[f] / n;
        for (const Row* r : rows)
            for (std::size_t f = 0; f < F; ++f) sd[f] += ((*r)[f] - mean[f]) * ((*r)[f] - mean[f]) / n;
        for (double& s : sd) s = s > 0 ? std::sqrt(s) : 1;
    }
    Row apply(const Row& r) const {
        Row o(r.size());
        for (std::size_t f = 0; f < r.size(); ++f) o[f] = (r[f] - mean[f]) / sd[f];
        return o;
    }
};

inline double dist2(const Row& a, const Row& b) {
    double s = 0;
    for (std::size_t f = 0; f < a.size(); ++f) s += (a[f] - b[f]) * (a[f] - b[f]);
    return s;
}

struct KMeans {
    std::vector<Row> centers;
    std::vector<std::size_t> label;
    double inertia = 0;
};

// Lloyd's iterations from k-means++ seeds; deterministic for a seed.
inline KMeans kmeans(const std::vector<const Row*>& x, std::size_t K, std::uint64_t seed = 1, int iters = 100) {
    KMeans km;
    const std::size_t n = x.size();
    if (n < K || K == 0) return km;
    const rng::Stream rng(seed, 0, 0);
    km.centers.push_back(*x[static_cast<std::size_t>(rng.uniform(0, 0) * static_cast<double>(n)) % n]);
    std::vector<double> d(n);
    for (std::size_t c = 1; c < K; ++c) {
        double total = 0;
        for (std::size_t i = 0; i < n; ++i) {
            d[i] = std::numeric_limits<double>::infinity();
            for (const Row& m : km.centers) d[i] = std::min(d[i], dist2(*x[i], m));
            total += d[i];
        }
        double u = rng.uniform(static_cast<std::uint32_t>(c), 0) * total;
        std::size_t pick = 0;
        while (pick + 1 < n && u > d[pick]) u -= d[pick++];
        km.centers.push_back(*x[pick]);
    }
    km.label.assign(n, 0);
    for (int it = 0; it < iters; ++it) {
        bool moved = false;
        km.inertia = 0;
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t best = 0;
            double bd = dist2(*x[i], km.centers[0]);
            for (std::size_t c = 1; c < K; ++c)
                if (const double dc = dist2(*x[i], km.centers[c]); dc < bd) bd = dc, best = c;
            moved |= best != km.label[i];
            km.label[i] = best, km.inertia += bd;
        }
        std::vector<Row> sum(K, Row(x[0]->size(), 0));
        std::vector<double> cnt(K, 0);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t f = 0; f < x[i]->size(); ++f) sum[km.label[i]][f] += (*x[i])[f];
            cnt[km.label[i]] += 1;
        }
        for (std::size_t c = 0; c < K; ++c)
            if (cnt[c] > 0)
                for (std::size_t f = 0; f < sum[c].size(); ++f) km.centers[c][f] = sum[c][f] / cnt[c];
        if (!moved && it) break;
    }
    return km;
}

struct Hmm {
    std::size_t K = 0, F = 0;
    std::vector<double> pi, A, mean, var;  // A[i * K + j] = P(j next | i now); mean, var: K x F
    double loglik = NAN;
    bool ok = false;
    // Free parameters, for BIC = -2 loglik + params log n.
    double params() const { return static_cast<double>((K - 1) + K * (K - 1) + 2 * K * F); }
    double log_emission(std::size_t k, const Row& x) const {
        double s = 0;
        for (std::size_t f = 0; f < F; ++f) {
            const double v = var[k * F + f], e = x[f] - mean[k * F + f];
            s += -0.5 * (std::log(2 * std::numbers::pi * v) + e * e / v);
        }
        return s;
    }
};

namespace detail {

// Emission likelihoods scaled per period by their maximum: b[t][k] = exp(log b - m_t).
inline std::vector<std::vector<double>> emissions(const Hmm& h, const Seq& s, std::vector<double>& m) {
    std::vector<std::vector<double>> b(s.size(), std::vector<double>(h.K));
    m.assign(s.size(), 0);
    for (std::size_t t = 0; t < s.size(); ++t) {
        double mx = -std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < h.K; ++k) mx = std::max(mx, b[t][k] = h.log_emission(k, s[t]));
        for (std::size_t k = 0; k < h.K; ++k) b[t][k] = std::exp(b[t][k] - mx);
        m[t] = mx;
    }
    return b;
}

// Scaled forward pass: alpha[t] = P(state_t | x_1..t); returns log P(x_1..T).
inline double forward(const Hmm& h, const std::vector<std::vector<double>>& b, const std::vector<double>& m,
                      std::vector<std::vector<double>>& alpha, std::vector<double>& c) {
    const std::size_t T = b.size(), K = h.K;
    alpha.assign(T, std::vector<double>(K, 0));
    c.assign(T, 0);
    double ll = 0;
    for (std::size_t t = 0; t < T; ++t) {
        for (std::size_t j = 0; j < K; ++j) {
            double p = 0;
            if (t == 0) p = h.pi[j];
            else
                for (std::size_t i = 0; i < K; ++i) p += alpha[t - 1][i] * h.A[i * K + j];
            alpha[t][j] = p * b[t][j];
            c[t] += alpha[t][j];
        }
        if (!(c[t] > 0)) c[t] = 1e-300;
        for (double& a : alpha[t]) a /= c[t];
        ll += std::log(c[t]) + m[t];
    }
    return ll;
}

}  // namespace detail

// Baum-Welch over sequences of standardised rows; variances floored at var_floor.
inline Hmm fit_hmm(const std::vector<Seq>& seqs, std::size_t K, std::uint64_t seed = 1, int iters = 200, double var_floor = 1e-3) {
    Hmm h;
    std::vector<const Row*> all;
    for (const Seq& s : seqs)
        for (const Row& r : s) all.push_back(&r);
    if (all.size() < 10 * K || K == 0) return h;
    h.K = K, h.F = all[0]->size();
    const std::size_t F = h.F;
    const KMeans km = kmeans(all, K, seed);
    h.mean.assign(K * F, 0), h.var.assign(K * F, 0);
    std::vector<double> cnt(K, 0);
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::size_t k = km.label[i];
        cnt[k] += 1;
        for (std::size_t f = 0; f < F; ++f) h.mean[k * F + f] += (*all[i])[f];
    }
    for (std::size_t k = 0; k < K; ++k)
        for (std::size_t f = 0; f < F; ++f) h.mean[k * F + f] = cnt[k] > 0 ? h.mean[k * F + f] / cnt[k] : km.centers[k][f];
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::size_t k = km.label[i];
        for (std::size_t f = 0; f < F; ++f) {
            const double e = (*all[i])[f] - h.mean[k * F + f];
            h.var[k * F + f] += e * e / std::max(cnt[k], 1.0);
        }
    }
    for (double& v : h.var) v = std::max(v, var_floor);
    h.pi.assign(K, 1.0 / static_cast<double>(K));
    h.A.assign(K * K, K > 1 ? 0.1 / static_cast<double>(K - 1) : 1.0);
    for (std::size_t k = 0; k < K; ++k) h.A[k * K + k] = K > 1 ? 0.9 : 1.0;

    double prev = -std::numeric_limits<double>::infinity();
    for (int it = 0; it < iters; ++it) {
        std::vector<double> pi(K, 0), An(K * K, 0), Ad(K, 0), g(K, 0), gx(K * F, 0), gxx(K * F, 0);
        double ll = 0;
        for (const Seq& s : seqs) {
            if (s.empty()) continue;
            const std::size_t T = s.size();
            std::vector<double> m, c;
            std::vector<std::vector<double>> alpha;
            const auto b = detail::emissions(h, s, m);
            ll += detail::forward(h, b, m, alpha, c);
            std::vector<std::vector<double>> beta(T, std::vector<double>(K, 1.0));
            for (std::size_t t = T - 1; t-- > 0;)
                for (std::size_t i = 0; i < K; ++i) {
                    double v = 0;
                    for (std::size_t j = 0; j < K; ++j) v += h.A[i * K + j] * b[t + 1][j] * beta[t + 1][j];
                    beta[t][i] = v / c[t + 1];
                }
            for (std::size_t t = 0; t < T; ++t) {
                for (std::size_t k = 0; k < K; ++k) {
                    const double gamma = alpha[t][k] * beta[t][k];
                    if (t == 0) pi[k] += gamma;
                    g[k] += gamma;
                    if (t + 1 < T) Ad[k] += gamma;
                    for (std::size_t f = 0; f < F; ++f) gx[k * F + f] += gamma * s[t][f], gxx[k * F + f] += gamma * s[t][f] * s[t][f];
                }
                if (t + 1 < T)
                    for (std::size_t i = 0; i < K; ++i)
                        for (std::size_t j = 0; j < K; ++j)
                            An[i * K + j] += alpha[t][i] * h.A[i * K + j] * b[t + 1][j] * beta[t + 1][j] / c[t + 1];
            }
        }
        h.loglik = ll;
        double pis = 0;
        for (const double p : pi) pis += p;
        for (std::size_t k = 0; k < K; ++k) {
            h.pi[k] = pis > 0 ? pi[k] / pis : 1.0 / static_cast<double>(K);
            double row = 0;
            for (std::size_t j = 0; j < K; ++j) row += An[k * K + j];
            for (std::size_t j = 0; j < K; ++j) h.A[k * K + j] = row > 0 ? An[k * K + j] / row : (j == k ? 1.0 : 0.0);
            if (g[k] <= 0) continue;
            for (std::size_t f = 0; f < F; ++f) {
                const double mu = gx[k * F + f] / g[k];
                h.mean[k * F + f] = mu;
                h.var[k * F + f] = std::max(gxx[k * F + f] / g[k] - mu * mu, var_floor);
            }
        }
        if (ll - prev < 1e-7 * (1 + std::abs(ll))) break;
        prev = ll;
    }
    h.ok = std::isfinite(h.loglik);
    return h;
}

// P(state_t | x_1..t) for every t.
inline std::vector<std::vector<double>> filter(const Hmm& h, const Seq& s) {
    std::vector<double> m, c;
    std::vector<std::vector<double>> alpha;
    if (s.empty()) return alpha;
    detail::forward(h, detail::emissions(h, s, m), m, alpha, c);
    return alpha;
}

inline double loglik(const Hmm& h, const Seq& s) {
    std::vector<double> m, c;
    std::vector<std::vector<double>> alpha;
    return s.empty() ? 0 : detail::forward(h, detail::emissions(h, s, m), m, alpha, c);
}

// The most likely state path.
inline std::vector<std::size_t> viterbi(const Hmm& h, const Seq& s) {
    const std::size_t T = s.size(), K = h.K;
    std::vector<std::size_t> path(T);
    if (!T) return path;
    std::vector<double> v(K), nv(K);
    std::vector<std::vector<std::size_t>> back(T, std::vector<std::size_t>(K));
    for (std::size_t k = 0; k < K; ++k) v[k] = std::log(h.pi[k]) + h.log_emission(k, s[0]);
    for (std::size_t t = 1; t < T; ++t) {
        for (std::size_t j = 0; j < K; ++j) {
            double best = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < K; ++i)
                if (const double x = v[i] + std::log(h.A[i * K + j]); x > best) best = x, back[t][j] = i;
            nv[j] = best + h.log_emission(j, s[t]);
        }
        v = nv;
    }
    path[T - 1] = static_cast<std::size_t>(std::max_element(v.begin(), v.end()) - v.begin());
    for (std::size_t t = T - 1; t > 0; --t) path[t - 1] = back[t][path[t]];
    return path;
}

// Relabels states in increasing order of feature f's mean, so state 0 is e.g. the calmest.
inline void sort_states(Hmm& h, std::size_t f) {
    const std::size_t K = h.K, F = h.F;
    std::vector<std::size_t> o(K);
    for (std::size_t k = 0; k < K; ++k) o[k] = k;
    std::sort(o.begin(), o.end(), [&](std::size_t a, std::size_t b) { return h.mean[a * F + f] < h.mean[b * F + f]; });
    Hmm s = h;
    for (std::size_t a = 0; a < K; ++a) {
        s.pi[a] = h.pi[o[a]];
        for (std::size_t b = 0; b < K; ++b) s.A[a * K + b] = h.A[o[a] * K + o[b]];
        for (std::size_t x = 0; x < F; ++x) s.mean[a * F + x] = h.mean[o[a] * F + x], s.var[a * F + x] = h.var[o[a] * F + x];
    }
    h = s;
}

}  // namespace lobiq::strategy::regime
