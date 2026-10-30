#pragma once

// Nelder-Mead minimisation over R^n (reflection 1, expansion 2, contraction and shrink 1/2), for
// small smooth likelihoods where gradients are not worth deriving. Constraints are imposed by f
// returning a huge value or, better, by optimising over transformed parameters.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace hft::opt {

struct Result {
    std::vector<double> x;
    double f = 0;
    int iterations = 0;
};

template <class F>
Result nelder_mead(F&& f, const std::vector<double>& x0, double step = 0.5, int max_iter = 4000, double tol = 1e-10) {
    const std::size_t n = x0.size();
    std::vector<std::vector<double>> s(n + 1, x0);
    std::vector<double> v(n + 1);
    for (std::size_t i = 1; i <= n; ++i) s[i][i - 1] += step;
    for (std::size_t i = 0; i <= n; ++i) v[i] = f(s[i]);
    std::vector<std::size_t> o(n + 1);
    std::vector<double> c(n), p(n);
    auto along = [&](double g, std::size_t worst) {
        for (std::size_t k = 0; k < n; ++k) p[k] = c[k] + g * (s[worst][k] - c[k]);
        return f(p);
    };
    int it = 0;
    for (; it < max_iter; ++it) {
        std::iota(o.begin(), o.end(), std::size_t{0});
        std::sort(o.begin(), o.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
        const std::size_t best = o[0], worst = o[n], second = o[n - 1];
        if (std::abs(v[worst] - v[best]) < tol * (1 + std::abs(v[best]))) break;
        std::fill(c.begin(), c.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t k = 0; k < n; ++k) c[k] += s[o[i]][k] / static_cast<double>(n);
        const double fr = along(-1, worst);
        const std::vector<double> r = p;
        if (fr < v[best]) {
            const double fe = along(-2, worst);
            if (fe < fr) s[worst] = p, v[worst] = fe;
            else s[worst] = r, v[worst] = fr;
        } else if (fr < v[second]) {
            s[worst] = r, v[worst] = fr;
        } else {
            const double fk = along(0.5, worst);
            if (fk < v[worst]) {
                s[worst] = p, v[worst] = fk;
            } else {
                for (std::size_t i = 1; i <= n; ++i) {
                    for (std::size_t k = 0; k < n; ++k) s[o[i]][k] = s[best][k] + 0.5 * (s[o[i]][k] - s[best][k]);
                    v[o[i]] = f(s[o[i]]);
                }
            }
        }
    }
    const std::size_t m = static_cast<std::size_t>(std::min_element(v.begin(), v.end()) - v.begin());
    return {s[m], v[m], it};
}

}  // namespace hft::opt
