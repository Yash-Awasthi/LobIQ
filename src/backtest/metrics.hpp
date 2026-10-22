#pragma once

// Risk-adjusted statistics of a backtest's per-minute PnL path: Sharpe, drawdown, the
// probabilistic Sharpe ratio and, over the runs of a sweep, the deflated Sharpe ratio
// (Bailey and Lopez de Prado 2012, 2014; docs/METHODS.md "Evaluation"). studio/metrics.py
// computes the same numbers from the report files; Studio.EndToEnd checks that they agree.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace hft::backtest {

struct RiskStats {
    std::size_t minutes = 0;
    double sharpe = 0;         // per minute: mean / sample sd of minute PnL changes
    double sharpe_annual = 0;  // sharpe * sqrt(252 * 390)
    double skew = 0, kurtosis = 3;
    double psr = std::numeric_limits<double>::quiet_NaN();  // P(true Sharpe > 0)
    double max_drawdown_usd = 0;
    std::size_t longest_underwater = 0;  // minutes
    // Set by deflate() over the runs of one sweep; a lone run has dsr == psr.
    std::size_t trials = 1;
    double sharpe_hurdle = 0;  // per minute: best Sharpe expected from luck over `trials`
    double dsr = std::numeric_limits<double>::quiet_NaN();
};

inline double normal_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

// Acklam's rational approximation (relative error below 1.2e-9) for 0 < p < 1.
inline double normal_quantile(double p) {
    static constexpr double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                                   1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
    static constexpr double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                                   6.680131188771972e+01,  -1.328068155288572e+01};
    static constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                                   -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
    static constexpr double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                                   3.754408661907416e+00};
    constexpr double lo = 0.02425;
    if (p < lo) {
        const double q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > 1 - lo) return -normal_quantile(1 - p);
    const double q = p - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

// P(true per-step Sharpe > benchmark) from n observations with the given skewness and kurtosis.
inline double probabilistic_sharpe(double sr, std::size_t n, double skew, double kurtosis, double benchmark = 0) {
    if (n < 2) return std::numeric_limits<double>::quiet_NaN();
    const double denom = 1 - skew * sr + (kurtosis - 1) / 4 * sr * sr;
    if (denom <= 0) return std::numeric_limits<double>::quiet_NaN();
    return normal_cdf((sr - benchmark) * std::sqrt(static_cast<double>(n - 1)) / std::sqrt(denom));
}

// Expected best Sharpe among k unskilled trials whose Sharpes have the sample spread of `srs`.
inline double expected_max_sharpe(const std::vector<double>& srs) {
    const std::size_t k = srs.size();
    if (k < 2) return 0;
    double m = 0;
    for (const double x : srs) m += x;
    m /= static_cast<double>(k);
    double v = 0;
    for (const double x : srs) v += (x - m) * (x - m);
    const double sd = std::sqrt(v / static_cast<double>(k - 1));
    constexpr double euler_gamma = 0.5772156649015329;
    const double kk = static_cast<double>(k);
    return sd * ((1 - euler_gamma) * normal_quantile(1 - 1 / kk) + euler_gamma * normal_quantile(1 - 1 / (kk * std::exp(1.0))));
}

// minute_pnl: cumulative total PnL in micro-dollars at the end of each minute (Summary::minute_pnl).
inline RiskStats risk_stats(const std::vector<std::int64_t>& minute_pnl) {
    RiskStats r;
    const std::size_t n = minute_pnl.size();
    r.minutes = n;
    if (n == 0) return r;
    std::vector<double> steps(n);
    for (std::size_t i = 0; i < n; ++i)
        steps[i] = static_cast<double>(minute_pnl[i] - (i ? minute_pnl[i - 1] : 0)) * 1e-6;
    double peak = 0, cum = 0;
    std::size_t under = 0;
    for (const double x : steps) {
        cum += x;
        if (cum >= peak) peak = cum, under = 0;
        else r.longest_underwater = std::max(r.longest_underwater, ++under);
        r.max_drawdown_usd = std::max(r.max_drawdown_usd, peak - cum);
    }
    if (n < 2) return r;
    double m = 0;
    for (const double x : steps) m += x;
    m /= static_cast<double>(n);
    double d2 = 0, d3 = 0, d4 = 0;
    for (const double x : steps) {
        const double d = x - m;
        d2 += d * d, d3 += d * d * d, d4 += d * d * d * d;
    }
    const double pop = d2 / static_cast<double>(n);
    if (pop > 0) {
        const double sd = std::sqrt(d2 / static_cast<double>(n - 1));
        r.sharpe = m / sd;
        r.skew = d3 / static_cast<double>(n) / std::pow(pop, 1.5);
        r.kurtosis = d4 / static_cast<double>(n) / (pop * pop);
    }
    r.sharpe_annual = r.sharpe * std::sqrt(252.0 * 390.0);
    r.psr = probabilistic_sharpe(r.sharpe, n, r.skew, r.kurtosis);
    r.dsr = r.psr;
    return r;
}

// The runs of one sweep are one set of trials: each run's DSR is its PSR against the hurdle.
inline void deflate(std::vector<RiskStats>& runs) {
    std::vector<double> srs;
    for (const RiskStats& r : runs) srs.push_back(r.sharpe);
    const double hurdle = expected_max_sharpe(srs);
    for (RiskStats& r : runs) {
        r.trials = runs.size();
        r.sharpe_hurdle = hurdle;
        r.dsr = probabilistic_sharpe(r.sharpe, r.minutes, r.skew, r.kurtosis, hurdle);
    }
}

}  // namespace hft::backtest
