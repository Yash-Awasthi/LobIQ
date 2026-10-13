#pragma once

// Estimators for the microstructure study (apps/itch_study.cpp): order flow imbalance at the best
// quotes (Cont, Kukanov and Stoikov 2014) and the micro-price adjustment (Stoikov 2018).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "book/types.hpp"

namespace hft::strategy::micro {

// Order flow at the best quotes between two consecutive books, in shares (the paper's e_n).
inline std::int64_t ofi_event(const book::Bbo& prev, const book::Bbo& cur) {
    std::int64_t e = 0;
    if (cur.bid_px >= prev.bid_px) e += cur.bid_qty;
    if (cur.bid_px <= prev.bid_px) e -= prev.bid_qty;
    if (cur.ask_px <= prev.ask_px) e -= cur.ask_qty;
    if (cur.ask_px >= prev.ask_px) e += prev.ask_qty;
    return e;
}

inline constexpr int kBuckets = 10, kSpreads = 2, kStates = kBuckets * kSpreads;

// imb = bid size / (bid + ask size) in [0, 1]; spread in ticks, 1 or 2.
inline int state_of(int spread_ticks, double imb) {
    const int b = imb >= 1 ? kBuckets - 1 : static_cast<int>(imb * kBuckets);
    return (spread_ticks - 1) * kBuckets + b;
}
// The same spread with the book seen from the other side.
inline int mirror(int x) { return (x / kBuckets) * kBuckets + (kBuckets - 1 - x % kBuckets); }

// Transitions between consecutive samples, each also counted mirrored with the mid move negated,
// so the estimate is symmetric by construction.
struct Counts {
    std::array<std::array<double, kStates>, kStates> q{}, t{};  // mid unchanged / mid moved
    std::array<double, kStates> n{}, r{};                        // transitions, summed mid moves

    void add(int from, int to, double dmid) {
        for (int m = 0; m < 2; ++m) {
            const auto a = static_cast<std::size_t>(m ? mirror(from) : from);
            const auto c = static_cast<std::size_t>(m ? mirror(to) : to);
            n[a] += 1;
            if (dmid == 0) {
                q[a][c] += 1;
            } else {
                t[a][c] += 1;
                r[a] += m ? -dmid : dmid;
            }
        }
    }
    void merge(const Counts& o) {
        for (std::size_t x = 0; x < kStates; ++x) {
            n[x] += o.n[x];
            r[x] += o.r[x];
            for (std::size_t y = 0; y < kStates; ++y) q[x][y] += o.q[x][y], t[x][y] += o.t[x][y];
        }
    }
};

// Solves A x = y by Gauss-Jordan with partial pivoting; a singular column (a state never seen)
// leaves its unknown at 0.
template <std::size_t N>
std::array<double, N> solve(std::array<std::array<double, N>, N> a, std::array<double, N> y) {
    for (std::size_t c = 0; c < N; ++c) {
        std::size_t p = c;
        for (std::size_t r = c + 1; r < N; ++r)
            if (std::abs(a[r][c]) > std::abs(a[p][c])) p = r;
        if (std::abs(a[p][c]) < 1e-12) continue;
        std::swap(a[p], a[c]);
        std::swap(y[p], y[c]);
        for (std::size_t r = 0; r < N; ++r) {
            if (r == c) continue;
            const double f = a[r][c] / a[c][c];
            for (std::size_t k = c; k < N; ++k) a[r][k] -= f * a[c][k];
            y[r] -= f * y[c];
        }
    }
    std::array<double, N> x{};
    for (std::size_t i = 0; i < N; ++i) x[i] = std::abs(a[i][i]) < 1e-12 ? 0 : y[i] / a[i][i];
    return x;
}

// Expected total mid move from a state, in the units of the counted moves:
// G = sum of g_k, g_1 = (1 - Q)^-1 R, g_{k+1} = (1 - Q)^-1 T g_k (Stoikov 2018, eq. 3.5).
inline std::array<double, kStates> adjustment(const Counts& c, int terms = 8) {
    std::array<std::array<double, kStates>, kStates> iq{}, t{};
    std::array<double, kStates> r{};
    for (std::size_t i = 0; i < kStates; ++i) {
        iq[i][i] = 1;
        const double n = c.n[i];
        if (n == 0) continue;
        for (std::size_t j = 0; j < kStates; ++j) iq[i][j] -= c.q[i][j] / n, t[i][j] = c.t[i][j] / n;
        r[i] = c.r[i] / n;
    }
    std::array<double, kStates> g = solve(iq, r), total = g;
    for (int k = 1; k < terms; ++k) {
        std::array<double, kStates> tg{};
        for (std::size_t i = 0; i < kStates; ++i)
            for (std::size_t j = 0; j < kStates; ++j) tg[i] += t[i][j] * g[j];
        g = solve(iq, tg);
        for (std::size_t i = 0; i < kStates; ++i) total[i] += g[i];
    }
    return total;
}

struct Fit {
    double a = 0, b = 0, r2 = 0;  // y = a + b x
    std::size_t n = 0;
};
inline Fit ols(const std::vector<double>& x, const std::vector<double>& y) {
    Fit f;
    f.n = x.size();
    if (f.n < 3) return f;
    double mx = 0, my = 0;
    for (std::size_t i = 0; i < f.n; ++i) mx += x[i], my += y[i];
    mx /= static_cast<double>(f.n), my /= static_cast<double>(f.n);
    double sxx = 0, sxy = 0, syy = 0;
    for (std::size_t i = 0; i < f.n; ++i)
        sxx += (x[i] - mx) * (x[i] - mx), sxy += (x[i] - mx) * (y[i] - my), syy += (y[i] - my) * (y[i] - my);
    if (sxx == 0 || syy == 0) return f;
    f.b = sxy / sxx;
    f.a = my - f.b * mx;
    f.r2 = sxy * sxy / (sxx * syy);
    return f;
}
// NaN entries are skipped; NaN when none is left.
inline double median(std::vector<double> v) {
    std::erase_if(v, [](double x) { return std::isnan(x); });
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    const std::size_t h = v.size() / 2;
    return v.size() % 2 ? v[h] : (v[h - 1] + v[h]) / 2;
}
// R^2 of fixed predictions against y: 1 - SSE / SST, negative when worse than y's own mean.
inline double r2_of(const std::vector<double>& pred, const std::vector<double>& y) {
    if (y.size() < 3) return NAN;
    double my = 0;
    for (double v : y) my += v;
    my /= static_cast<double>(y.size());
    double sse = 0, sst = 0;
    for (std::size_t i = 0; i < y.size(); ++i) sse += (y[i] - pred[i]) * (y[i] - pred[i]), sst += (y[i] - my) * (y[i] - my);
    return sst > 0 ? 1 - sse / sst : NAN;
}

// Lead-lag: a mid move of one symbol (timestamp in ns, direction +1 / -1).
struct Move {
    std::uint64_t ts;
    int dir;
};
inline constexpr std::uint64_t kLeadHorizon = 1'000'000'000;  // later answers do not count
// Upper edges of the delay bins for same-direction answers, ns.
inline constexpr std::array<std::uint64_t, 6> kLeadEdge = {1'000, 10'000, 100'000, 1'000'000, 10'000'000, kLeadHorizon};

struct LeadResponse {
    std::uint64_t leads = 0, answered = 0, same = 0;
    std::array<std::uint64_t, kLeadEdge.size()> bins{};
    std::vector<std::uint64_t> delays;  // same-direction delays, ns
    void add(const LeadResponse& o) {
        leads += o.leads, answered += o.answered, same += o.same;
        for (std::size_t i = 0; i < bins.size(); ++i) bins[i] += o.bins[i];
        delays.insert(delays.end(), o.delays.begin(), o.delays.end());
    }
};

// For every move of a (shifted by `shift`; seconds give a placebo with no fast information): the
// first move of b strictly after it, within kLeadHorizon, and whether it goes sign * a's way.
inline LeadResponse lead_response(const std::vector<Move>& a, const std::vector<Move>& b, int sign,
                                  std::uint64_t shift = 0) {
    LeadResponse r;
    std::size_t j = 0;
    for (const Move& mv : a) {
        const std::uint64_t t = mv.ts + shift;
        ++r.leads;
        while (j < b.size() && b[j].ts <= t) ++j;
        if (j == b.size()) break;
        const std::uint64_t d = b[j].ts - t;
        if (d > kLeadHorizon) continue;
        ++r.answered;
        if (b[j].dir != sign * mv.dir) continue;
        ++r.same;
        r.delays.push_back(d);
        std::size_t k = 0;
        while (k + 1 < kLeadEdge.size() && d > kLeadEdge[k]) ++k;
        ++r.bins[k];
    }
    return r;
}

// Exponential Hawkes process on [0, horizon]: intensity mu + alpha * sum exp(-beta (t - t_i)) over
// earlier events. Branching ratio alpha / beta: the share of events triggered by earlier ones.
struct HawkesFit {
    double mu = 0, alpha = 0, beta = 0, loglik = 0;
    bool ok = false;
};

inline double hawkes_loglik(const std::vector<double>& t, double horizon, double mu, double alpha, double beta) {
    double ll = 0, a = 0, prev = 0, tail = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (i) a = std::exp(-beta * (t[i] - prev)) * (1 + a);
        ll += std::log(mu + alpha * a);
        tail += 1 - std::exp(-beta * (horizon - t[i]));
        prev = t[i];
    }
    return ll - mu * horizon - alpha / beta * tail;
}

// Maximum likelihood by Nelder-Mead on (log mu, log alpha, log beta), stationary (alpha < beta).
inline HawkesFit fit_hawkes(const std::vector<double>& t, double horizon) {
    HawkesFit best;
    if (t.size() < 20 || horizon <= 0) return best;
    using P = std::array<double, 3>;
    auto f = [&](const P& x) {
        const double mu = std::exp(x[0]), alpha = std::exp(x[1]), beta = std::exp(x[2]);
        if (alpha >= beta || !std::isfinite(mu + alpha + beta)) return 1e300;
        return -hawkes_loglik(t, horizon, mu, alpha, beta);
    };
    const double rate = static_cast<double>(t.size()) / horizon;
    double best_val = 1e300;
    // A few starting memories, since the likelihood can be flat in beta.
    for (const double beta0 : {rate, 10 * rate, 0.1 * rate}) {
        std::array<P, 4> s;
        s[0] = {std::log(0.5 * rate), std::log(0.5 * beta0), std::log(beta0)};
        for (int i = 1; i < 4; ++i) {
            s[static_cast<std::size_t>(i)] = s[0];
            s[static_cast<std::size_t>(i)][static_cast<std::size_t>(i - 1)] += 0.5;
        }
        std::array<double, 4> v;
        for (std::size_t i = 0; i < 4; ++i) v[i] = f(s[i]);
        for (int it = 0; it < 600; ++it) {
            std::array<std::size_t, 4> o = {0, 1, 2, 3};
            std::sort(o.begin(), o.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
            if (std::abs(v[o[3]] - v[o[0]]) < 1e-9 * (1 + std::abs(v[o[0]]))) break;
            P c{};
            for (int i = 0; i < 3; ++i)
                for (std::size_t k = 0; k < 3; ++k) c[k] += s[o[static_cast<std::size_t>(i)]][k] / 3;
            auto along = [&](double g) {
                P p;
                for (std::size_t k = 0; k < 3; ++k) p[k] = c[k] + g * (s[o[3]][k] - c[k]);
                return p;
            };
            const P r = along(-1);
            const double fr = f(r);
            if (fr < v[o[0]]) {
                const P e = along(-2);
                const double fe = f(e);
                s[o[3]] = fe < fr ? e : r, v[o[3]] = std::min(fe, fr);
            } else if (fr < v[o[2]]) {
                s[o[3]] = r, v[o[3]] = fr;
            } else {
                const P k = along(0.5);
                const double fk = f(k);
                if (fk < v[o[3]]) {
                    s[o[3]] = k, v[o[3]] = fk;
                } else {
                    for (std::size_t i = 1; i < 4; ++i) {
                        for (std::size_t q = 0; q < 3; ++q) s[o[i]][q] = s[o[0]][q] + 0.5 * (s[o[i]][q] - s[o[0]][q]);
                        v[o[i]] = f(s[o[i]]);
                    }
                }
            }
        }
        const std::size_t m = static_cast<std::size_t>(std::min_element(v.begin(), v.end()) - v.begin());
        if (v[m] < best_val) {
            best_val = v[m];
            best = {std::exp(s[m][0]), std::exp(s[m][1]), std::exp(s[m][2]), -v[m], true};
        }
    }
    best.ok = best_val < 1e299;
    return best;
}

// VPIN (Easley, Lopez de Prado and O'Hara 2012) on exact trade signs: executions filled into
// buckets of equal volume; each bucket's order imbalance |buy - sell| / V and the mid at its close.
struct SignedTrade {
    double shares;
    int dir;     // +1 the taker bought
    double mid;  // mid before the trade
};
struct VolumeBucket {
    double imbalance, mid;
};
inline std::vector<VolumeBucket> volume_buckets(const std::vector<SignedTrade>& trades, double volume) {
    std::vector<VolumeBucket> out;
    double room = volume, buy = 0, sell = 0;
    for (const SignedTrade& t : trades)
        for (double q = t.shares; q > 0;) {
            const double take = std::min(q, room);
            (t.dir > 0 ? buy : sell) += take;
            q -= take, room -= take;
            if (room <= 1e-9 * volume) {
                out.push_back({std::abs(buy - sell) / volume, t.mid});
                room = volume, buy = sell = 0;
            }
        }
    return out;
}

}  // namespace hft::strategy::micro
