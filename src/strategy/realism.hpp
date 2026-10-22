#pragma once

// Distances between two order flow samples, as LOB-Bench scores generated against real order books
// (Nagy et al. 2025, github.com/peernagy/lob_bench; docs/METHODS.md "Market realism"): total
// variation between binned histograms (LOB-Bench's L1, halved to lie in [0, 1]) and the Wasserstein-1
// distance between distributions standardised by the reference sample's mean and deviation.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hft::strategy::realism {

// Keeps a uniform sample of at most `cap` values from a stream (reservoir sampling, fixed seed).
struct Reservoir {
    explicit Reservoir(std::size_t cap = 200'000) : cap_(cap) {}
    void add(double x) {
        ++seen_;
        if (v.size() < cap_) return v.push_back(x);
        state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
        const std::uint64_t j = (state_ >> 11) % seen_;
        if (j < cap_) v[j] = x;
    }
    std::vector<double> v;
    std::uint64_t seen() const { return seen_; }

   private:
    std::size_t cap_;
    std::uint64_t seen_ = 0, state_ = 0x9e3779b97f4a7c15ull;
};

inline double quantile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return NAN;
    const double pos = p * static_cast<double>(sorted.size() - 1);
    const auto i = static_cast<std::size_t>(pos);
    const double f = pos - static_cast<double>(i);
    return i + 1 < sorted.size() ? sorted[i] * (1 - f) + sorted[i + 1] * f : sorted[i];
}

inline void mean_sd(const std::vector<double>& x, double& m, double& sd) {
    m = 0, sd = 0;
    if (x.empty()) return;
    for (const double v : x) m += v;
    m /= static_cast<double>(x.size());
    for (const double v : x) sd += (v - m) * (v - m);
    sd = x.size() > 1 ? std::sqrt(sd / static_cast<double>(x.size() - 1)) : 0;
}

// W1 between the two empirical distributions after (x - mean) / sd with the reference's mean and sd,
// from their quantile functions at 1,000 points.
inline double wasserstein(std::vector<double> ref, std::vector<double> other) {
    if (ref.empty() || other.empty()) return NAN;
    std::sort(ref.begin(), ref.end()), std::sort(other.begin(), other.end());
    double m, sd;
    mean_sd(ref, m, sd);
    if (sd == 0) sd = 1;
    double d = 0;
    constexpr int kPoints = 1000;
    for (int i = 0; i < kPoints; ++i) {
        const double p = (i + 0.5) / kPoints;
        d += std::fabs(quantile(ref, p) - quantile(other, p));
    }
    return d / kPoints / sd;
}

// Total variation between histograms over the pooled range, in [0, 1]: 0 identical, 1 disjoint.
inline double total_variation(const std::vector<double>& a, const std::vector<double>& b, int bins = 50) {
    if (a.empty() || b.empty()) return NAN;
    const auto [amin, amax] = std::minmax_element(a.begin(), a.end());
    const auto [bmin, bmax] = std::minmax_element(b.begin(), b.end());
    const double lo = std::min(*amin, *bmin), hi = std::max(*amax, *bmax);
    if (hi == lo) return 0;
    std::vector<double> ha(static_cast<std::size_t>(bins), 0), hb(static_cast<std::size_t>(bins), 0);
    auto bin = [&](double x) { return std::min<std::size_t>(static_cast<std::size_t>((x - lo) / (hi - lo) * bins), static_cast<std::size_t>(bins - 1)); };
    for (const double x : a) ha[bin(x)] += 1.0 / static_cast<double>(a.size());
    for (const double x : b) hb[bin(x)] += 1.0 / static_cast<double>(b.size());
    double tv = 0;
    for (int i = 0; i < bins; ++i) tv += std::fabs(ha[static_cast<std::size_t>(i)] - hb[static_cast<std::size_t>(i)]);
    return tv / 2;
}

// Sample autocorrelation at `lag`.
inline double acf(const std::vector<double>& x, std::size_t lag) {
    if (x.size() <= lag + 1) return NAN;
    double m, sd;
    mean_sd(x, m, sd);
    double num = 0, den = 0;
    for (std::size_t i = 0; i < x.size(); ++i) den += (x[i] - m) * (x[i] - m);
    for (std::size_t i = lag; i < x.size(); ++i) num += (x[i] - m) * (x[i - lag] - m);
    return den > 0 ? num / den : NAN;
}

// Kurtosis (non-excess): 3 for a normal distribution, more for fat tails.
inline double kurtosis(const std::vector<double>& x) {
    double m, sd;
    mean_sd(x, m, sd);
    if (x.size() < 4 || sd == 0) return NAN;
    double s4 = 0, s2 = 0;
    for (const double v : x) s4 += std::pow(v - m, 4), s2 += (v - m) * (v - m);
    const double n = static_cast<double>(x.size());
    return (s4 / n) / ((s2 / n) * (s2 / n));
}

}  // namespace hft::strategy::realism
