#pragma once

// Multi-level order flow imbalance and its cross-impact (Cont, Cucuringu and Zhang 2023,
// docs/METHODS.md "Multi-level OFI"): the best-level OFI of Cont, Kukanov and Stoikov applied to the
// m-th best prices, combined into one "integrated" OFI by the first principal component, and a
// ridge regression for forecasting with several symbols' lagged OFI.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace hft::strategy::mlofi {

inline constexpr int kLevels = 10;

struct Levels {
    std::array<std::uint32_t, kLevels> bid_px{}, ask_px{};  // 0: no level
    std::array<std::uint64_t, kLevels> bid_q{}, ask_q{};
};

// The top kLevels of each side of a book with best_level / next_level_info (book::TickBook).
template <class Book, class Side>
void snapshot(const Book& b, Side bid, Side ask, Levels& out) {
    out = Levels{};
    auto fill = [&](Side side, auto& px, auto& q) {
        auto l = b.best_level(side);
        for (int m = 0; m < kLevels && l; ++m, l = b.next_level_info(side, l->price)) px[m] = l->price, q[m] = l->qty;
    };
    fill(bid, out.bid_px, out.bid_q);
    fill(ask, out.ask_px, out.ask_q);
}

// Adds each level's order flow between two consecutive books to acc (the paper's e^m_n, in shares).
// A missing ask level counts as an infinitely high price, a missing bid level as zero.
inline void add_ofi(const Levels& prev, const Levels& cur, std::array<double, kLevels>& acc) {
    constexpr std::uint64_t kNone = ~std::uint32_t{0};
    for (int m = 0; m < kLevels; ++m) {
        const std::uint64_t pb = prev.bid_px[m], cb = cur.bid_px[m];
        const std::uint64_t pa = prev.ask_px[m] ? prev.ask_px[m] : kNone, ca = cur.ask_px[m] ? cur.ask_px[m] : kNone;
        double e = 0;
        if (cb >= pb) e += static_cast<double>(cur.bid_q[m]);
        if (cb <= pb) e -= static_cast<double>(prev.bid_q[m]);
        if (ca <= pa) e -= static_cast<double>(cur.ask_q[m]);
        if (ca >= pa) e += static_cast<double>(prev.ask_q[m]);
        acc[m] += e;
    }
}

// Mean shares per level over both sides: the paper's depth normaliser for one book.
inline double depth(const Levels& l) {
    double s = 0;
    int n = 0;
    for (int m = 0; m < kLevels; ++m) {
        if (l.bid_px[m]) s += static_cast<double>(l.bid_q[m]), ++n;
        if (l.ask_px[m]) s += static_cast<double>(l.ask_q[m]), ++n;
    }
    return n ? s / n : 0;
}

// First principal component of the rows' covariance (power iteration), signed to sum positive and
// scaled to unit L1 norm, so the integrated OFI is a weighted average of the levels. `share` gets
// the fraction of total variance it explains.
inline std::array<double, kLevels> first_pc(const std::vector<std::array<double, kLevels>>& rows, double* share = nullptr) {
    std::array<double, kLevels> w{};
    const std::size_t n = rows.size();
    if (n < 2) {
        w[0] = 1;
        return w;
    }
    std::array<double, kLevels> mean{};
    for (const auto& r : rows)
        for (int i = 0; i < kLevels; ++i) mean[i] += r[i] / static_cast<double>(n);
    std::array<std::array<double, kLevels>, kLevels> c{};
    for (const auto& r : rows)
        for (int i = 0; i < kLevels; ++i)
            for (int j = 0; j < kLevels; ++j) c[i][j] += (r[i] - mean[i]) * (r[j] - mean[j]) / static_cast<double>(n - 1);
    w.fill(1.0);
    double lambda = 0;
    for (int it = 0; it < 500; ++it) {
        std::array<double, kLevels> v{};
        for (int i = 0; i < kLevels; ++i)
            for (int j = 0; j < kLevels; ++j) v[i] += c[i][j] * w[j];
        double norm = 0;
        for (const double x : v) norm += x * x;
        norm = std::sqrt(norm);
        if (norm == 0) break;
        for (int i = 0; i < kLevels; ++i) w[i] = v[i] / norm;
        lambda = norm;
    }
    double sum = 0, l1 = 0, trace = 0;
    for (int i = 0; i < kLevels; ++i) sum += w[i], l1 += std::fabs(w[i]), trace += c[i][i];
    if (l1 == 0) {
        w.fill(0);
        w[0] = 1;
        return w;
    }
    for (double& x : w) x = (sum < 0 ? -x : x) / l1;
    if (share) *share = trace > 0 ? lambda / trace : 0;
    return w;
}

// Ridge regression with an intercept: y ~ b0 + X b. The intercept is not penalised; X's columns are
// used as given. Returns {b0, b1, ...}; Gaussian elimination with partial pivoting.
inline std::vector<double> ridge(const std::vector<std::vector<double>>& X, const std::vector<double>& y, double lambda) {
    const std::size_t k = X.empty() ? 0 : X[0].size(), p = k + 1;
    std::vector<std::vector<double>> a(p, std::vector<double>(p + 1, 0));
    for (std::size_t t = 0; t < X.size(); ++t) {
        std::vector<double> row(p, 1);
        for (std::size_t j = 0; j < k; ++j) row[j + 1] = X[t][j];
        for (std::size_t i = 0; i < p; ++i) {
            for (std::size_t j = 0; j < p; ++j) a[i][j] += row[i] * row[j];
            a[i][p] += row[i] * y[t];
        }
    }
    for (std::size_t i = 1; i < p; ++i) a[i][i] += lambda;
    for (std::size_t col = 0; col < p; ++col) {
        std::size_t piv = col;
        for (std::size_t r = col + 1; r < p; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        std::swap(a[col], a[piv]);
        if (std::fabs(a[col][col]) < 1e-12) continue;
        for (std::size_t r = 0; r < p; ++r) {
            if (r == col) continue;
            const double f = a[r][col] / a[col][col];
            for (std::size_t j = col; j <= p; ++j) a[r][j] -= f * a[col][j];
        }
    }
    std::vector<double> b(p, 0);
    for (std::size_t i = 0; i < p; ++i) b[i] = std::fabs(a[i][i]) < 1e-12 ? 0 : a[i][p] / a[i][i];
    return b;
}

inline double predict(const std::vector<double>& b, const std::vector<double>& x) {
    double v = b[0];
    for (std::size_t j = 0; j < x.size(); ++j) v += b[j + 1] * x[j];
    return v;
}

}  // namespace hft::strategy::mlofi
