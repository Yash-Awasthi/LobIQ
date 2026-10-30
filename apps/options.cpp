// Options pricing and the implied volatility surface (src/options/, docs/METHODS.md "Options").
//   options price call|put S K T r q sigma
//       Black-Scholes price and Greeks, the cash-or-nothing digital, a CRR binomial tree (European and
//       American) and Monte Carlo with its standard error, side by side.
//   options surface QUOTES.csv [--rate r] [--greeks]
//       CSV columns expiry_years,strike,type (C or P),bid,ask. Per expiry the forward comes from
//       put-call parity, implied volatilities from out-of-the-money mids (Black-76), and an SVI slice
//       is fitted to them; prints the slices, their arbitrage checks and a volatility grid. Markdown.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "options/black_scholes.hpp"
#include "options/lattice.hpp"
#include "options/monte_carlo.hpp"
#include "options/svi.hpp"
#include "strategy/microstructure.hpp"

namespace {

using namespace lobiq::options;

constexpr const char* kUsage =
    "usage: options price call|put S K T r q sigma\n"
    "       options surface QUOTES.csv [--rate r] [--greeks]   (columns expiry_years,strike,type,bid,ask)";

int price(int argc, char** argv) {
    if (argc != 9) return std::fprintf(stderr, "%s\n", kUsage), 2;
    const std::string k = argv[2];
    if (k != "call" && k != "put") return std::fprintf(stderr, "%s\n", kUsage), 2;
    const Kind kind = k == "call" ? Kind::Call : Kind::Put;
    double v[6];
    for (int i = 0; i < 6; ++i) v[i] = std::atof(argv[3 + i]);
    const double S = v[0], K = v[1], T = v[2], r = v[3], q = v[4], sigma = v[5];
    if (!(S > 0 && K > 0 && T > 0 && sigma > 0)) return std::fprintf(stderr, "S, K, T and sigma must be positive\n"), 2;
    const Greeks g = black_scholes(kind, S, K, T, r, q, sigma), dg = digital(kind, S, K, T, r, q, sigma);
    const TreeResult eu = binomial(kind, false, S, K, T, r, q, sigma, 2000), am = binomial(kind, true, S, K, T, r, q, sigma, 2000);
    const McResult mc = monte_carlo(kind, S, K, T, r, q, sigma);
    std::printf("# %s S=%g K=%g T=%g r=%g q=%g sigma=%g\n\n", k.c_str(), S, K, T, r, q, sigma);
    std::printf("| Model | price | delta | gamma | vega | theta (per year) | rho |\n|---|---|---|---|---|---|---|\n");
    std::printf("| Black-Scholes | %.6f | %.6f | %.6f | %.6f | %.6f | %.6f |\n", g.price, g.delta, g.gamma, g.vega, g.theta, g.rho);
    std::printf("| Digital (pays 1) | %.6f | %.6f | %.6f | %.6f | %.6f | %.6f |\n", dg.price, dg.delta, dg.gamma, dg.vega, dg.theta, dg.rho);
    std::printf("| Binomial, European (2000 steps) | %.6f | %.6f | %.6f | | | |\n", eu.price, eu.delta, eu.gamma);
    std::printf("| Binomial, American (2000 steps) | %.6f | %.6f | %.6f | | | |\n", am.price, am.delta, am.gamma);
    std::printf("| Monte Carlo (%zu paths) | %.6f ± %.6f | %.6f | | | | |\n", mc.paths, mc.price, mc.std_error, mc.delta);
    std::printf("\nVanna %.6f, volga %.6f. Early exercise premium %.6f. Implied volatility of the Black-Scholes price: %.6f.\n", g.vanna,
                g.volga, am.price - eu.price, implied_vol(kind, g.price, S, K, T, r, q));
    return 0;
}

struct Quote {
    double T, K, mid, iv = NAN, w = 0;
    Kind kind;
};

int surface(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "%s\n", kUsage), 2;
    double r = 0;
    bool greeks = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc) r = std::atof(argv[++i]);
        else if (a == "--greeks") greeks = true;
        else return std::fprintf(stderr, "%s\n", kUsage), 2;
    }
    std::ifstream f(argv[2]);
    if (!f) return std::fprintf(stderr, "cannot read %s\n", argv[2]), 2;
    std::string line;
    std::getline(f, line);
    std::map<double, std::vector<Quote>> by_t;
    std::size_t bad = 0;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string c[5];
        for (auto& x : c) std::getline(ss, x, ',');
        const double T = std::atof(c[0].c_str()), K = std::atof(c[1].c_str()), bid = std::atof(c[3].c_str()), ask = std::atof(c[4].c_str());
        const char t = c[2].empty() ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c[2][0])));
        if (!(T > 0 && K > 0 && bid > 0 && ask >= bid) || (t != 'C' && t != 'P')) {
            ++bad;
            continue;
        }
        by_t[T].push_back({T, K, 0.5 * (bid + ask), NAN, 0, t == 'C' ? Kind::Call : Kind::Put});
    }
    if (by_t.empty()) return std::fprintf(stderr, "no usable quotes in %s\n", argv[2]), 2;

    Surface surf;
    std::vector<double> forwards;
    std::vector<std::vector<Quote>> used;
    for (auto& [T, qs] : by_t) {
        // Forward from put-call parity at every strike with both: F = K + e^{rT} (C - P).
        std::map<double, std::pair<double, double>> cp;
        for (const Quote& q : qs) (q.kind == Kind::Call ? cp[q.K].first : cp[q.K].second) = q.mid;
        std::vector<double> fs;
        for (const auto& [K, x] : cp)
            if (x.first > 0 && x.second > 0) fs.push_back(K + std::exp(r * T) * (x.first - x.second));
        const double F = lobiq::strategy::micro::median(fs);
        if (!std::isfinite(F) || F <= 0) {
            std::fprintf(stderr, "expiry %g: no strike with both a call and a put; skipped\n", T);
            continue;
        }
        std::vector<Quote> otm;
        std::vector<double> k, vol, w;
        for (Quote q : qs) {
            if ((q.kind == Kind::Call) != (q.K >= F)) continue;  // out of the money only
            q.iv = implied_vol(q.kind, q.mid, F, q.K, T, r, r);   // Black-76: S = F, q = r
            if (!std::isfinite(q.iv)) continue;
            q.w = black_scholes(q.kind, F, q.K, T, r, r, q.iv).vega;
            otm.push_back(q);
        }
        double wsum = 0;
        for (const Quote& q : otm) wsum += q.w;
        for (const Quote& q : otm) k.push_back(std::log(q.K / F)), vol.push_back(q.iv), w.push_back(q.w / wsum);
        Svi s = fit_svi(k, vol, w, T);
        if (!s.ok) {
            std::fprintf(stderr, "expiry %g: %zu usable quotes, need 5; skipped\n", T, otm.size());
            continue;
        }
        s.forward = F;
        surf.slices.push_back(s);
        used.push_back(otm);
    }
    if (surf.slices.empty()) return std::fprintf(stderr, "no expiry could be fitted\n"), 1;

    std::printf("# Implied volatility surface\n\n%s; rate %g. Forward per expiry from put-call parity (median over strikes); implied "
                "volatility from out-of-the-money mids with Black-76; raw SVI per expiry, vega-weighted least squares in total variance. "
                "%zu quotes skipped as malformed. Method: docs/METHODS.md.\n\n",
                argv[2], r, bad);
    std::printf("| Expiry (y) | forward | quotes | ATM vol | ATM skew dvol/dk | a | b | rho | m | s | RMSE (vol) | min density g |\n"
                "|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    double kmin = 0, kmax = 0;  // over every expiry, so each slice is checked on the same range
    for (std::size_t i = 0; i < surf.slices.size(); ++i)
        for (const Quote& q : used[i])
            kmin = std::min(kmin, std::log(q.K / surf.slices[i].forward)), kmax = std::max(kmax, std::log(q.K / surf.slices[i].forward));
    for (std::size_t i = 0; i < surf.slices.size(); ++i) {
        const Svi& s = surf.slices[i];
        const double atm = s.vol(0), skew = s.w1(0) / (2 * atm * s.T);
        std::printf("| %.4f | %.4f | %zu | %.4f | %+.4f | %.5f | %.5f | %+.3f | %+.4f | %.4f | %.4f | %+.4f |\n", s.T, s.forward, used[i].size(),
                    atm, skew, s.a, s.b, s.rho, s.m, s.s, s.rmse_vol, min_density(s, kmin, kmax));
    }
    const std::size_t cal = surf.calendar_violations(kmin, kmax);
    std::printf("\nButterfly arbitrage where min density g < 0 within the quoted range of k = log(K/F) [%.3f, %.3f]; calendar arbitrage "
                "(total variance falling with expiry) at %zu of %zu grid points.\n",
                kmin, kmax, cal, 101 * (surf.slices.size() - 1));
    std::printf("\n## Implied volatility by log-moneyness\n\n| k |");
    for (const Svi& s : surf.slices) std::printf(" T=%.3f |", s.T);
    std::printf("\n|---|");
    for (std::size_t i = 0; i < surf.slices.size(); ++i) std::printf("---|");
    std::printf("\n");
    for (int j = -6; j <= 6; ++j) {
        const double k = 0.05 * j;
        std::printf("| %+.2f |", k);
        for (const Svi& s : surf.slices) std::printf(" %.4f |", s.vol(k));
        std::printf("\n");
    }
    if (greeks) {
        std::printf("\n## Quotes\n\n| Expiry | strike | type | mid | implied vol | SVI vol | delta | gamma | vega | theta |\n"
                    "|---|---|---|---|---|---|---|---|---|---|\n");
        for (std::size_t i = 0; i < used.size(); ++i)
            for (const Quote& q : used[i]) {
                const Greeks g = black_scholes(q.kind, surf.slices[i].forward, q.K, q.T, r, r, q.iv);
                std::printf("| %.4f | %g | %s | %.4f | %.4f | %.4f | %+.4f | %.5f | %.4f | %+.4f |\n", q.T, q.K, q.kind == Kind::Call ? "C" : "P",
                            q.mid, q.iv, surf.slices[i].vol(std::log(q.K / surf.slices[i].forward)), g.delta, g.gamma, g.vega, g.theta);
            }
        std::printf("\nGreeks are with respect to the forward (Black-76).\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "price") return price(argc, argv);
    if (argc >= 2 && std::string(argv[1]) == "surface") return surface(argc, argv);
    std::fprintf(stderr, "%s\n", kUsage);
    return 2;
}
