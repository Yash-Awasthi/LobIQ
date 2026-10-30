#pragma once

// Tests against overfitting a sweep or a model (docs/METHODS.md "Evaluation"): the stationary
// bootstrap (Politis and Romano 1994), Hansen's test for superior predictive ability (2005), the
// model confidence set (Hansen, Lunde and Nason 2011), the probability of backtest overfitting by
// combinatorially symmetric cross-validation (Bailey, Borwein, Lopez de Prado and Zhu 2017) and
// purged, embargoed k-fold splits (Lopez de Prado 2018).

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "core/philox.hpp"

namespace lobiq::backtest {

// One resample of 0..n-1: blocks of geometric length with mean `block`, starting anywhere, wrapping.
inline std::vector<std::size_t> stationary_bootstrap(std::size_t n, double block, const rng::Stream& rng, std::uint32_t draw) {
    std::vector<std::size_t> idx(n);
    const double p = 1 / std::max(block, 1.0);
    std::size_t at = 0;
    for (std::size_t t = 0; t < n; ++t) {
        const auto r = rng.draw(draw, static_cast<std::uint32_t>(t));
        const double u = (r[0] + 0.5) * 0x1p-32;
        if (t == 0 || u < p) at = static_cast<std::size_t>((r[1] + 0.5) * 0x1p-32 * static_cast<double>(n)) % n;
        else at = (at + 1) % n;
        idx[t] = at;
    }
    return idx;
}

// means[b][k]: mean of series k over bootstrap resample b; every series shares the resamples.
inline std::vector<std::vector<double>> bootstrap_means(const std::vector<std::vector<double>>& x, std::size_t reps, double block,
                                                        std::uint64_t seed) {
    const std::size_t n = x.empty() ? 0 : x[0].size();
    std::vector<std::vector<double>> out(reps, std::vector<double>(x.size(), 0));
    const rng::Stream rng(seed, 0, 0);
    // A resample's mean is a dot product with how often it drew each period: contiguous, vectorisable.
    std::vector<double> count(n);
    for (std::size_t b = 0; b < reps; ++b) {
        std::fill(count.begin(), count.end(), 0.0);
        for (const std::size_t i : stationary_bootstrap(n, block, rng, static_cast<std::uint32_t>(b))) count[i] += 1;
        for (std::size_t k = 0; k < x.size(); ++k) {
            const double* xk = x[k].data();
            double s = 0;
            for (std::size_t i = 0; i < n; ++i) s += count[i] * xk[i];
            out[b][k] = s / static_cast<double>(n);
        }
    }
    return out;
}

inline double mean_of(const std::vector<double>& v) {
    return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

struct SpaResult {
    double stat = 0;                       // max studentised mean excess, floored at 0
    double p_lower = 1, p_consistent = 1, p_upper = 1;  // p_upper is White's reality check
    std::size_t best = 0;                  // the model with the largest studentised mean
};

// d[k][t]: performance of model k over the benchmark at time t (higher is better), equal lengths.
// Null: no model beats the benchmark in expectation.
inline SpaResult spa_test(const std::vector<std::vector<double>>& d, std::size_t reps = 1000, double block = 10, std::uint64_t seed = 1) {
    SpaResult r;
    const std::size_t K = d.size(), n = K ? d[0].size() : 0;
    if (K == 0 || n < 10) return r;
    const auto means = bootstrap_means(d, reps, block, seed);
    std::vector<double> dbar(K), omega(K);
    const double rn = std::sqrt(static_cast<double>(n));
    for (std::size_t k = 0; k < K; ++k) {
        dbar[k] = mean_of(d[k]);
        double v = 0;
        for (std::size_t b = 0; b < reps; ++b) v += (means[b][k] - dbar[k]) * (means[b][k] - dbar[k]);
        omega[k] = std::sqrt(v / static_cast<double>(reps)) * rn;
    }
    double tmax = -std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < K; ++k) {
        const double t = omega[k] > 0 ? rn * dbar[k] / omega[k] : 0;
        if (t > tmax) tmax = t, r.best = k;
    }
    r.stat = std::max(tmax, 0.0);
    // Recentred means: lower keeps only models doing better than the benchmark, consistent drops
    // only models clearly worse, upper keeps them all.
    const double thresh = -std::sqrt(2 * std::log(std::log(static_cast<double>(n))));
    std::size_t lo = 0, co = 0, up = 0;
    for (std::size_t b = 0; b < reps; ++b) {
        double sl = 0, sc = 0, su = 0;
        for (std::size_t k = 0; k < K; ++k) {
            if (!(omega[k] > 0)) continue;
            const double z = rn * (means[b][k] - dbar[k]) / omega[k], m = rn * dbar[k] / omega[k];
            sl = std::max(sl, z + (m > 0 ? 0 : m));
            sc = std::max(sc, z + (m >= thresh ? 0 : m));
            su = std::max(su, z);
        }
        lo += sl >= r.stat, co += sc >= r.stat, up += su >= r.stat;
    }
    r.p_lower = static_cast<double>(lo) / static_cast<double>(reps);
    r.p_consistent = static_cast<double>(co) / static_cast<double>(reps);
    r.p_upper = static_cast<double>(up) / static_cast<double>(reps);
    return r;
}

// Model confidence set with the T_max statistic. loss[k][t], lower is better. Returns each model's
// MCS p-value: the model is in the (1 - alpha) set when its p-value is at least alpha.
inline std::vector<double> model_confidence_set(const std::vector<std::vector<double>>& loss, std::size_t reps = 500, double block = 10,
                                                std::uint64_t seed = 2) {
    const std::size_t K = loss.size();
    std::vector<double> p(K, 1.0);
    if (K < 2 || loss[0].size() < 10) return p;
    const auto means = bootstrap_means(loss, reps, block, seed);
    std::vector<double> lbar(K);
    for (std::size_t k = 0; k < K; ++k) lbar[k] = mean_of(loss[k]);
    std::vector<std::size_t> alive(K);
    std::iota(alive.begin(), alive.end(), std::size_t{0});
    double running = 0;
    while (alive.size() > 1) {
        const double m = static_cast<double>(alive.size());
        double avg = 0;
        for (const std::size_t k : alive) avg += lbar[k] / m;
        std::vector<double> avg_b(reps, 0);
        for (std::size_t b = 0; b < reps; ++b)
            for (const std::size_t k : alive) avg_b[b] += means[b][k] / m;
        double tmax = -std::numeric_limits<double>::infinity();
        std::size_t worst = 0;
        std::vector<double> sd(alive.size());
        for (std::size_t i = 0; i < alive.size(); ++i) {
            const std::size_t k = alive[i];
            const double di = lbar[k] - avg;
            double v = 0;
            for (std::size_t b = 0; b < reps; ++b) {
                const double e = means[b][k] - avg_b[b] - di;
                v += e * e;
            }
            sd[i] = std::sqrt(v / static_cast<double>(reps));
            const double t = sd[i] > 0 ? di / sd[i] : 0;
            if (t > tmax) tmax = t, worst = i;
        }
        std::size_t ge = 0;
        for (std::size_t b = 0; b < reps; ++b) {
            double mx = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < alive.size(); ++i) {
                const std::size_t k = alive[i];
                const double e = means[b][k] - avg_b[b] - (lbar[k] - avg);
                mx = std::max(mx, sd[i] > 0 ? e / sd[i] : 0);
            }
            ge += mx >= tmax;
        }
        running = std::max(running, static_cast<double>(ge) / static_cast<double>(reps));
        p[alive[worst]] = running;
        alive.erase(alive.begin() + static_cast<std::ptrdiff_t>(worst));
    }
    return p;
}

struct PboResult {
    double pbo = NAN;           // share of splits where the in-sample best ranks below the OOS median
    double oos_loss = NAN;      // share of splits where the in-sample best has a negative OOS Sharpe
    double median_rank = NAN;   // median OOS relative rank of the in-sample best, in (0, 1)
    std::size_t splits = 0;
};

// perf[k][t]: per-period return of configuration k. The periods are cut into `blocks` contiguous
// blocks (even, at most 16); every choice of half the blocks is one in-sample set, the rest out of
// sample, and the configuration with the best in-sample Sharpe is ranked out of sample.
inline PboResult pbo_cscv(const std::vector<std::vector<double>>& perf, std::size_t blocks = 10) {
    PboResult r;
    const std::size_t N = perf.size(), T = N ? perf[0].size() : 0;
    if (N < 2 || blocks < 2 || blocks % 2 || blocks > 16 || T < 2 * blocks) return r;
    // Per block and configuration: sum, sum of squares, count; a Sharpe over any set of blocks follows.
    std::vector<std::vector<std::array<double, 3>>> acc(blocks, std::vector<std::array<double, 3>>(N, {0, 0, 0}));
    for (std::size_t k = 0; k < N; ++k)
        for (std::size_t t = 0; t < T; ++t) {
            auto& a = acc[std::min(t * blocks / T, blocks - 1)][k];
            a[0] += perf[k][t], a[1] += perf[k][t] * perf[k][t], a[2] += 1;
        }
    auto sharpe = [&](std::uint32_t mask, std::size_t k) {
        double s = 0, q = 0, n = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            if (mask >> b & 1) s += acc[b][k][0], q += acc[b][k][1], n += acc[b][k][2];
        const double m = s / n, v = (q - n * m * m) / (n - 1);
        return v > 0 ? m / std::sqrt(v) : 0.0;
    };
    std::vector<double> ranks, is(N), oos(N);
    std::size_t over = 0, loss = 0;
    for (std::uint32_t mask = 0; mask < (1u << blocks); ++mask) {
        if (static_cast<std::size_t>(std::popcount(mask)) != blocks / 2) continue;
        const std::uint32_t rest = ((1u << blocks) - 1) & ~mask;
        for (std::size_t k = 0; k < N; ++k) is[k] = sharpe(mask, k), oos[k] = sharpe(rest, k);
        const std::size_t best = static_cast<std::size_t>(std::max_element(is.begin(), is.end()) - is.begin());
        double below = 0, tie = 0;
        for (std::size_t k = 0; k < N; ++k) below += oos[k] < oos[best], tie += oos[k] == oos[best];
        const double w = (below + (tie + 1) / 2) / static_cast<double>(N + 1);
        ranks.push_back(w);
        over += w <= 0.5;  // logit(w) <= 0
        loss += oos[best] < 0;
    }
    r.splits = ranks.size();
    r.pbo = static_cast<double>(over) / static_cast<double>(r.splits);
    r.oos_loss = static_cast<double>(loss) / static_cast<double>(r.splits);
    std::sort(ranks.begin(), ranks.end());
    const std::size_t h = ranks.size() / 2;
    r.median_rank = ranks.size() % 2 ? ranks[h] : (ranks[h - 1] + ranks[h]) / 2;
    return r;
}

struct Fold {
    std::vector<std::size_t> train, test;
};

// Samples sorted by start time; sample i is observed at start[i] and its label is known at end[i].
// The k contiguous test folds each train on every sample whose label ends before the fold starts or
// that starts more than `embargo` after the last test label ends, so no training label overlaps a
// test sample's information.
inline std::vector<Fold> purged_kfold(const std::vector<std::uint64_t>& start, const std::vector<std::uint64_t>& end, std::size_t k,
                                      std::uint64_t embargo) {
    const std::size_t n = start.size();
    std::vector<Fold> out;
    if (k < 2 || n < k) return out;
    for (std::size_t f = 0; f < k; ++f) {
        const std::size_t a = f * n / k, b = (f + 1) * n / k;
        Fold fold;
        std::uint64_t last_end = 0;
        for (std::size_t i = a; i < b; ++i) fold.test.push_back(i), last_end = std::max(last_end, end[i]);
        for (std::size_t i = 0; i < n; ++i) {
            if (i >= a && i < b) continue;
            if (end[i] < start[a] || start[i] > last_end + embargo) fold.train.push_back(i);
        }
        out.push_back(std::move(fold));
    }
    return out;
}

}  // namespace lobiq::backtest
