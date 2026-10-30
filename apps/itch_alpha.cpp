// A linear alpha on the backtester's streaming features, walk-forward (docs/METHODS.md "Alpha
// model"): per symbol, the mid change over the next --horizon seconds regressed (ridge) on the 23
// features sampled every --step seconds. The penalty is chosen by purged, embargoed k-fold
// cross-validation on the train days; split-conformal half-widths come from the out-of-fold residuals;
// everything is then scored unchanged on the test days. --model DIR writes DIR/SYMBOL.alpha for the
// `alpha:` strategy. --symbolic G also searches G generations for a closed-form formula on the same
// features, picked on the last fifth of the train days and scored on the test days. Prints Markdown.
// Usage: itch_alpha --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--horizon S] [--step S]
//                   [--folds K] [--miscoverage A] [--model DIR] [--symbolic GENERATIONS]

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "backtest/overfit.hpp"
#include "core/parallel.hpp"
#include "data/store.hpp"
#include "days.hpp"
#include "feed/itch.hpp"
#include "strategy/conformal.hpp"
#include "strategy/features.hpp"
#include "strategy/microstructure.hpp"
#include "strategy/mlofi.hpp"
#include "strategy/multi_features.hpp"
#include "strategy/symreg.hpp"

namespace {

using namespace hft;
namespace fs = std::filesystem;
using strategy::SymbolFeatures;

constexpr std::uint64_t kSec = 1'000'000'000ull, kDay = 86'400 * kSec;
constexpr std::uint64_t kOpen = (9 * 3600 + 35 * 60) * kSec, kClose = (15 * 3600 + 55 * 60) * kSec;
constexpr std::size_t kF = SymbolFeatures::kCount;

struct Sample {
    std::uint64_t ts;  // ns since midnight
    std::array<double, kF> x;
    double y;           // mid change over the horizon, ticks
    double half_spread;  // ticks
};

// Features exactly as itch_backtest computes them for a strategy, sampled on the grid; the label is
// the mid `ahead` grid steps later. Only labels the feed covers count: after the last message a flat
// mid is missing data, not a zero move.
std::vector<Sample> samples(const fs::path& dir, std::uint16_t loc, std::uint64_t step, std::size_t ahead) {
    strategy::FeatureParams fp;
    strategy::MultiFeatures mf(1, {}, fp);
    const std::size_t n = (kClose - kOpen) / step;
    std::vector<std::array<double, kF>> rows(n);
    std::vector<double> mid(n, NAN), half(n, NAN);
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    std::size_t next = 0;
    std::uint64_t last_ts = 0;
    auto take = [&] {
        mf.row(0, kOpen + next * step, rows[next].data());
        mid[next] = mf.symbol(0).mid();
        const book::Bbo q = mf.book(0).bbo();
        if (q.bid_px && q.ask_px > q.bid_px) half[next] = (q.ask_px - q.bid_px) / (2.0 * fp.tick);
        ++next;
    };
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        last_ts = ts;
        while (next < n && ts >= kOpen + next * step) take();
        if (next >= n) break;
        mf.on_itch(0, rec.data, rec.len, rec.seq);
    }
    while (next < n) take();
    std::vector<Sample> out;
    for (std::size_t k = 0; k + ahead < n && kOpen + (k + ahead) * step <= last_ts; ++k)
        if (std::isfinite(mid[k]) && std::isfinite(mid[k + ahead]))
            out.push_back({kOpen + k * step, rows[k], mid[k + ahead] - mid[k], half[k]});
    return out;
}

// Standardised and clipped like DpPolicy; a missing feature is the train mean.
struct Scaler {
    std::array<double, kF> mean{}, sd{};
    void fit(const std::vector<const Sample*>& s) {
        std::array<double, kF> n{};
        for (const Sample* p : s)
            for (std::size_t j = 0; j < kF; ++j)
                if (std::isfinite(p->x[j])) mean[j] += p->x[j], n[j] += 1;
        for (std::size_t j = 0; j < kF; ++j) mean[j] = n[j] > 0 ? mean[j] / n[j] : 0;
        for (const Sample* p : s)
            for (std::size_t j = 0; j < kF; ++j)
                if (std::isfinite(p->x[j])) sd[j] += (p->x[j] - mean[j]) * (p->x[j] - mean[j]);
        for (std::size_t j = 0; j < kF; ++j) sd[j] = n[j] > 1 && sd[j] > 0 ? std::sqrt(sd[j] / (n[j] - 1)) : 1;
    }
    std::vector<double> row(const Sample& p) const {
        std::vector<double> r(kF);
        for (std::size_t j = 0; j < kF; ++j) r[j] = std::isfinite(p.x[j]) ? std::clamp((p.x[j] - mean[j]) / sd[j], -5.0, 5.0) : 0;
        return r;
    }
};

// Normal equations of y ~ b0 + x b, accumulated once and solved for any penalty lambda * n on b
// (not b0): one pass over the samples serves the whole penalty grid.
struct Gram {
    static constexpr std::size_t P = kF + 1;
    std::array<std::array<double, P + 1>, P> a{};  // [X'X | X'y], X with a leading 1
    double n = 0;
    void add(const std::vector<double>& x, double y) {
        double r[P];
        r[0] = 1;
        for (std::size_t j = 0; j < kF; ++j) r[j + 1] = x[j];
        for (std::size_t i = 0; i < P; ++i) {
            for (std::size_t j = i; j < P; ++j) a[i][j] += r[i] * r[j];
            a[i][P] += r[i] * y;
        }
        n += 1;
    }
    std::vector<double> solve(double lambda) const {
        auto m = a;
        for (std::size_t i = 0; i < P; ++i)
            for (std::size_t j = 0; j < i; ++j) m[i][j] = m[j][i];
        for (std::size_t i = 1; i < P; ++i) m[i][i] += lambda * n;
        // Positive definite once penalised: elimination without pivoting.
        for (std::size_t c = 0; c < P; ++c) {
            if (std::abs(m[c][c]) < 1e-12) continue;
            for (std::size_t r = c + 1; r < P; ++r) {
                const double f = m[r][c] / m[c][c];
                for (std::size_t k = c; k <= P; ++k) m[r][k] -= f * m[c][k];
            }
        }
        std::vector<double> b(P, 0);
        for (std::size_t c = P; c-- > 0;) {
            if (std::abs(m[c][c]) < 1e-12) continue;
            double s = m[c][P];
            for (std::size_t k = c + 1; k < P; ++k) s -= m[c][k] * b[k];
            b[c] = s / m[c][c];
        }
        return b;
    }
};

Gram gram(const std::vector<const Sample*>& s, const Scaler& z) {
    Gram g;
    for (const Sample* p : s) g.add(z.row(*p), p->y);
    return g;
}

struct Result {
    double lambda = NAN, cv_r2 = NAN, q = NAN;
    double r2 = NAN, ic = NAN, hit = NAN, coverage = NAN, top_move = NAN, half = NAN;
    std::size_t n_train = 0, n_test = 0;
    Scaler z;
    std::vector<double> b;
    // Symbolic regression: the chosen formula and the accuracy-complexity front it came from.
    std::string formula;
    std::size_t formula_size = 0;
    double sym_valid = NAN, sym_r2 = NAN, sym_ic = NAN;
    std::vector<std::array<double, 3>> front;  // size, fit R², validation R²
};

double corr(const std::vector<double>& a, const std::vector<double>& b);

// Standardised feature columns and target of the samples.
strategy::symreg::Data columns(const std::vector<const Sample*>& s, const Scaler& z, std::size_t max_size) {
    std::vector<std::vector<double>> cols(kF, std::vector<double>(s.size()));
    std::vector<double> y(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto r = z.row(*s[i]);
        for (std::size_t j = 0; j < kF; ++j) cols[j][i] = r[j];
        y[i] = s[i]->y;
    }
    return strategy::symreg::Data(std::move(cols), std::move(y), max_size);
}

// Chronological split of the train samples: the search fits on the first 80% (thinned to at most
// 20,000 rows, purged of labels reaching into the rest), the front is ranked on the last 20%.
void symbolic(Result& o, const std::vector<const Sample*>& tr, const std::vector<std::uint64_t>& start,
              const std::vector<std::uint64_t>& end, const std::vector<const Sample*>& te, std::size_t generations, std::uint64_t seed) {
    namespace sr = strategy::symreg;
    const std::size_t cut = tr.size() * 4 / 5;
    std::vector<const Sample*> fit, valid(tr.begin() + static_cast<std::ptrdiff_t>(cut), tr.end());
    const std::size_t stride = std::max<std::size_t>(1, cut / 20000);
    for (std::size_t i = 0; i < cut; i += stride)
        if (end[i] < start[cut]) fit.push_back(tr[i]);
    if (fit.size() < 500 || valid.size() < 100 || te.size() < 100) return;
    Scaler z;
    z.fit(fit);
    sr::Params prm;
    prm.generations = generations, prm.seed = seed;
    sr::Data df = columns(fit, z, prm.max_size), dv = columns(valid, z, prm.max_size), dt = columns(te, z, prm.max_size);
    std::vector<double> yv, yt;
    for (const Sample* p : valid) yv.push_back(p->y);
    for (const Sample* p : te) yt.push_back(p->y);
    const auto hof = sr::evolve(df, prm);
    // The front keeps a size only if it fits better than every smaller formula.
    std::vector<const sr::Formula*> front;
    std::vector<double> val;
    double floor = std::numeric_limits<double>::infinity(), top = -std::numeric_limits<double>::infinity();
    for (const sr::Formula& f : hof) {
        if (!(f.mse < floor)) continue;
        floor = f.mse;
        const double v = strategy::micro::r2_of(dv.predict(f), yv);
        front.push_back(&f), val.push_back(v);
        o.front.push_back({static_cast<double>(f.size()), 1 - f.mse * static_cast<double>(df.rows()) / df.sst(), v});
        if (std::isfinite(v)) top = std::max(top, v);
    }
    // Parsimony: the smallest formula whose validation error is within 0.5% of the best one's.
    const sr::Formula* best = nullptr;
    for (std::size_t k = 0; k < front.size() && !best; ++k)
        if (std::isfinite(val[k]) && 1 - val[k] <= 1.005 * (1 - top)) best = front[k], o.sym_valid = val[k];
    if (!best) return;
    std::vector<std::string> names(SymbolFeatures::kNames.begin(), SymbolFeatures::kNames.end());
    o.formula = sr::to_string(best->prog, names);
    o.formula_size = best->size();
    const auto pt = dt.predict(*best);
    o.sym_r2 = strategy::micro::r2_of(pt, yt);
    o.sym_ic = corr(pt, yt);
}

double corr(const std::vector<double>& a, const std::vector<double>& b) {
    const double n = static_cast<double>(a.size());
    double ma = 0, mb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) ma += a[i] / n, mb += b[i] / n;
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) sab += (a[i] - ma) * (b[i] - mb), saa += (a[i] - ma) * (a[i] - ma), sbb += (b[i] - mb) * (b[i] - mb);
    return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : NAN;
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage =
        "usage: itch_alpha --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--horizon S] [--step S] [--folds K] "
        "[--miscoverage A] [--model DIR] [--symbolic GENERATIONS]";
    std::vector<fs::path> train, test;
    std::string symbols = "10", model_dir;
    double horizon_s = 5, step_s = 1, miscoverage = 0.1;
    std::size_t folds = 5, generations = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return std::fprintf(stderr, "missing value for %s\n%s\n", a.c_str(), usage), 2;
        const std::string v = argv[++i];
        if (a == "--train") train = app::paths(v);
        else if (a == "--test") test = app::paths(v);
        else if (a == "--symbols") symbols = v;
        else if (a == "--horizon") horizon_s = std::atof(v.c_str());
        else if (a == "--step") step_s = std::atof(v.c_str());
        else if (a == "--folds") folds = std::strtoul(v.c_str(), nullptr, 10);
        else if (a == "--miscoverage") miscoverage = std::atof(v.c_str());
        else if (a == "--model") model_dir = v;
        else if (a == "--symbolic") generations = std::strtoul(v.c_str(), nullptr, 10);
        else return std::fprintf(stderr, "unknown option %s\n%s\n", a.c_str(), usage), 2;
    }
    const double ratio = horizon_s / step_s;
    if (train.empty() || test.empty() || !(step_s >= 0.01) || !(ratio >= 1) || std::abs(ratio - std::round(ratio)) > 1e-9 || folds < 2 ||
        !(miscoverage > 0 && miscoverage < 1))
        return std::fprintf(stderr, "%s\n(--horizon must be a whole number of --step)\n", usage), 2;
    std::vector<fs::path> days = train;
    days.insert(days.end(), test.begin(), test.end());
    for (const auto& d : days)
        if (!fs::is_directory(d)) return std::fprintf(stderr, "not a store: %s\n", d.c_str()), 2;
    const app::Universe u = app::universe(days, symbols);
    if (!u.error.empty()) return std::fprintf(stderr, "%s\n", u.error.c_str()), 2;
    const std::size_t S = u.names.size(), D = days.size(), T = train.size();
    const auto step = static_cast<std::uint64_t>(step_s * 1e9), h = static_cast<std::uint64_t>(horizon_s * 1e9);
    const auto ahead = static_cast<std::size_t>(std::round(ratio));

    std::vector<std::vector<Sample>> sets(S * D);
    parallel_for(S * D, [&](std::size_t i) { sets[i] = samples(days[i % D], u.locate[i % D].at(u.names[i / D]), step, ahead); });

    const std::vector<double> lambdas = {1e-4, 1e-3, 1e-2, 1e-1, 1, 10};
    std::vector<Result> res(S);
    parallel_for(S, [&](std::size_t s) {
        Result& o = res[s];
        std::vector<const Sample*> tr;
        std::vector<std::uint64_t> start, end;
        for (std::size_t d = 0; d < T; ++d)
            for (const Sample& p : sets[s * D + d]) tr.push_back(&p), start.push_back(d * kDay + p.ts), end.push_back(d * kDay + p.ts + h);
        o.n_train = tr.size();
        if (tr.size() < 50 * folds) return;
        const auto cv = backtest::purged_kfold(start, end, folds, h);
        // Out-of-fold predictions per penalty; each fold's scaler sees only its training samples.
        std::vector<std::vector<double>> oof(lambdas.size(), std::vector<double>(tr.size(), NAN));
        for (const backtest::Fold& f : cv) {
            std::vector<const Sample*> fit;
            for (const std::size_t i : f.train) fit.push_back(tr[i]);
            Scaler z;
            z.fit(fit);
            const Gram g = gram(fit, z);
            std::vector<std::vector<double>> rows;
            for (const std::size_t i : f.test) rows.push_back(z.row(*tr[i]));
            for (std::size_t l = 0; l < lambdas.size(); ++l) {
                const auto b = g.solve(lambdas[l]);
                for (std::size_t k = 0; k < rows.size(); ++k) oof[l][f.test[k]] = strategy::mlofi::predict(b, rows[k]);
            }
        }
        std::vector<double> ytr;
        for (const Sample* p : tr) ytr.push_back(p->y);
        std::size_t best = 0;
        for (std::size_t l = 0; l < lambdas.size(); ++l) {
            const double r2 = strategy::micro::r2_of(oof[l], ytr);
            if (l == 0 || r2 > o.cv_r2) o.cv_r2 = r2, best = l;
        }
        o.lambda = lambdas[best];
        std::vector<double> scores;
        for (std::size_t i = 0; i < tr.size(); ++i) scores.push_back(std::abs(ytr[i] - oof[best][i]));
        o.q = strategy::conformal::split_quantile(scores, miscoverage);
        o.z.fit(tr);
        o.b = gram(tr, o.z).solve(o.lambda);

        std::vector<double> pred, y, half;
        for (std::size_t d = T; d < D; ++d)
            for (const Sample& p : sets[s * D + d]) {
                pred.push_back(strategy::mlofi::predict(o.b, o.z.row(p))), y.push_back(p.y);
                if (std::isfinite(p.half_spread)) half.push_back(p.half_spread);
            }
        o.n_test = y.size();
        if (y.size() < 10) return;
        o.r2 = strategy::micro::r2_of(pred, y);
        o.ic = corr(pred, y);
        double hits = 0, moved = 0, covered = 0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            if (y[i] != 0) moved += 1, hits += (y[i] > 0) == (pred[i] > 0);
            covered += std::abs(y[i] - pred[i]) <= o.q;
        }
        o.hit = moved > 0 ? hits / moved : NAN;
        o.coverage = covered / static_cast<double>(y.size());
        // The most confident tenth of the forecasts: the move they caught against the cost of crossing.
        std::vector<std::size_t> idx(y.size());
        for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return std::abs(pred[a]) > std::abs(pred[b]); });
        double caught = 0;
        const std::size_t top = std::max<std::size_t>(1, idx.size() / 10);
        for (std::size_t k = 0; k < top; ++k) caught += (pred[idx[k]] > 0 ? 1 : -1) * y[idx[k]];
        o.top_move = caught / static_cast<double>(top);
        o.half = strategy::micro::median(half);
        if (generations) {
            std::vector<const Sample*> te;
            for (std::size_t d = T; d < D; ++d)
                for (const Sample& p : sets[s * D + d]) te.push_back(&p);
            symbolic(o, tr, start, end, te, generations, s + 1);
        }
    });

    std::printf("# Alpha model\n\n%zu symbols; train %zu days, test %zu days. Target: mid change over the next %g s (ticks), sampled every %g s "
                "from 09:35 to 15:55 ET. Ridge regression on the backtester's %zu streaming features, standardised on the train days; "
                "penalty by %zu-fold purged cross-validation with a %g s embargo. Conformal half-width at %.0f%% coverage from the "
                "out-of-fold residuals. Test days are out of sample. Method: docs/METHODS.md.\n\n",
                S, T, D - T, horizon_s, step_s, kF, folds, horizon_s, 100 * (1 - miscoverage));
    std::printf("| Symbol | train samples | penalty | CV R² | test R² | test IC | sign hit rate | interval ± (ticks) | test coverage | "
                "top-decile move (ticks) | median half spread (ticks) |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
    std::vector<double> r2, ic, cov;
    for (std::size_t s = 0; s < S; ++s) {
        const Result& o = res[s];
        if (o.b.empty()) {
            std::printf("| %s | %zu | too few samples | | | | | | | | |\n", u.names[s].c_str(), o.n_train);
            continue;
        }
        std::printf("| %s | %zu | %g | %.4f | %.4f | %.3f | %.3f | %.2f | %.3f | %+.3f | %.2f |\n", u.names[s].c_str(), o.n_train, o.lambda,
                    o.cv_r2, o.r2, o.ic, o.hit, o.q, o.coverage, o.top_move, o.half);
        r2.push_back(o.r2), ic.push_back(o.ic), cov.push_back(o.coverage);
    }
    using strategy::micro::median;
    if (!r2.empty()) std::printf("| median | | | | %.4f | %.3f | | | %.3f | | |\n", median(r2), median(ic), median(cov));
    std::printf("\nA forecast pays to trade on only when its top-decile move exceeds the half spread plus fees; otherwise it can still "
                "skew passive quotes (strategy `alpha:`).\n");

    if (generations) {
        std::printf("\n## Symbolic regression\n\nGenetic programming over the same standardised features (+, -, *, protected /, negation, "
                    "tanh; at most 25 nodes; %zu generations of 400), each candidate linearly scaled to the target. The sizes whose best formula fits "
                    "better than every smaller one form the accuracy-complexity front; on the last fifth of the train days the smallest "
                    "formula within 0.5%% of the best validation error is chosen and scored on the test days. Ridge's test R² for comparison.\n\n"
                    "| Symbol | nodes | validation R² | test R² | test IC | ridge test R² | formula |\n|---|---|---|---|---|---|---|\n",
                    generations);
        for (std::size_t s = 0; s < S; ++s) {
            const Result& o = res[s];
            if (o.formula.empty()) {
                std::printf("| %s | too few samples | | | | | |\n", u.names[s].c_str());
                continue;
            }
            std::printf("| %s | %zu | %.4f | %.4f | %.3f | %.4f | `%s` |\n", u.names[s].c_str(), o.formula_size, o.sym_valid, o.sym_r2, o.sym_ic,
                        o.r2, o.formula.c_str());
        }
        for (std::size_t s = 0; s < S; ++s) {
            if (res[s].front.empty()) continue;
            std::printf("\nFront for %s:\n\n| nodes | fit R² | validation R² |\n|---|---|---|\n", u.names[s].c_str());
            for (const auto& f : res[s].front) std::printf("| %.0f | %.4f | %.4f |\n", f[0], f[1], f[2]);
        }
    }

    if (!model_dir.empty()) {
        fs::create_directories(model_dir);
        for (std::size_t s = 0; s < S; ++s) {
            const Result& o = res[s];
            if (o.b.empty()) continue;
            const std::string path = model_dir + "/" + u.names[s] + ".alpha";
            std::FILE* f = std::fopen(path.c_str(), "w");
            if (!f) return std::fprintf(stderr, "cannot write %s\n", path.c_str()), 1;
            std::fprintf(f, "LOBIQ-ALPHA 1\nhorizon_s %g\nlambda %g\nhalf_width %.9g\nmiscoverage %g\nintercept %.9g\nfeatures %zu\n", horizon_s,
                         o.lambda, o.q, miscoverage, o.b[0], kF);
            for (std::size_t j = 0; j < kF; ++j)
                std::fprintf(f, "%s %.9g %.9g %.9g\n", SymbolFeatures::kNames[j], o.z.mean[j], o.z.sd[j], o.b[j + 1]);
            std::fclose(f);
        }
        std::fprintf(stderr, "models: %s/<SYMBOL>.alpha\n", model_dir.c_str());
    }
    return 0;
}
