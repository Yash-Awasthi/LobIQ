// Volatility forecasts, walk-forward (src/strategy/garch.hpp): per symbol, GARCH(1,1) with normal
// and Student-t innovations fitted on the train days' mid changes over a fixed interval, and a HAR
// model on per-minute realized variance, scored on the test days against the EWMA the market makers
// use (60 s time constant). Prints Markdown, and the avellaneda_stoikov keys of each fit.
// Usage: itch_vol --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--interval SECONDS]

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "core/parallel.hpp"
#include "data/store.hpp"
#include "days.hpp"
#include "feed/itch.hpp"
#include "strategy/garch.hpp"
#include "strategy/microstructure.hpp"

namespace {

using namespace hft;
namespace fs = std::filesystem;
namespace vol = strategy::vol;

constexpr std::uint64_t kSec = 1'000'000'000ull;
constexpr std::uint64_t kOpen = (9 * 3600 + 35 * 60) * kSec, kClose = (15 * 3600 + 55 * 60) * kSec;
constexpr double kEwmaTau = 60;  // seconds, AvellanedaStoikov::var_tau_s

// Mid changes in ticks over the grid; the mid at a boundary is the last one before it.
std::vector<double> returns(const fs::path& dir, std::uint16_t loc, std::uint64_t step) {
    const std::size_t n = (kClose - kOpen) / step;
    std::vector<double> mid(n + 1, NAN);
    book::TickBook b;
    book::ItchApply<book::TickBook> ap{b};
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    double last = NAN;
    std::size_t next = 0;
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        while (next <= n && ts >= kOpen + next * step) mid[next++] = last;
        if (next > n) break;
        ap.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, ap);
        const book::Bbo q = b.bbo();
        if (q.bid_px && q.ask_px && q.ask_px > q.bid_px) last = (static_cast<double>(q.bid_px) + q.ask_px) / 200.0;
    }
    while (next <= n) mid[next++] = last;
    std::vector<double> r(n, 0);
    for (std::size_t k = 0; k < n; ++k)
        if (std::isfinite(mid[k]) && std::isfinite(mid[k + 1])) r[k] = mid[k + 1] - mid[k];
    return r;
}

std::vector<double> minute_rv(const std::vector<double>& r, std::size_t per_min) {
    std::vector<double> rv(r.size() / per_min, 0);
    for (std::size_t k = 0; k < rv.size() * per_min; ++k) rv[k / per_min] += r[k] * r[k];
    return rv;
}

struct Result {
    vol::GarchFit n, t;
    double ll_gain = NAN;  // test log-likelihood per interval, Student-t less normal
    double d_int = NAN, dm_int = NAN, d_har = NAN, dm_har = NAN, d_gm = NAN, dm_gm = NAN;
};

double mean_diff(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0;
    for (std::size_t i = 0; i < a.size(); ++i) s += a[i] - b[i];
    return a.empty() ? NAN : s / static_cast<double>(a.size());
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage =
        "usage: itch_vol --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--interval SECONDS (divides 60)]";
    std::vector<fs::path> train, test;
    std::string symbols = "10";
    std::uint64_t interval = 10;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return std::fprintf(stderr, "missing value for %s\n%s\n", a.c_str(), usage), 2;
        const std::string v = argv[++i];
        if (a == "--train") train = app::paths(v);
        else if (a == "--test") test = app::paths(v);
        else if (a == "--symbols") symbols = v;
        else if (a == "--interval") interval = std::strtoull(v.c_str(), nullptr, 10);
        else return std::fprintf(stderr, "unknown option %s\n%s\n", a.c_str(), usage), 2;
    }
    if (train.empty() || test.empty() || interval == 0 || 60 % interval) return std::fprintf(stderr, "%s\n", usage), 2;
    std::vector<fs::path> days = train;
    days.insert(days.end(), test.begin(), test.end());
    for (const auto& d : days)
        if (!fs::is_directory(d)) return std::fprintf(stderr, "not a store: %s\n", d.c_str()), 2;
    const app::Universe u = app::universe(days, symbols);
    if (!u.error.empty()) return std::fprintf(stderr, "%s\n", u.error.c_str()), 2;
    const std::size_t S = u.names.size(), D = days.size(), T = train.size(), per_min = 60 / interval;
    const std::uint64_t step = interval * kSec;

    std::vector<std::vector<double>> r(S * D);
    parallel_for(S * D, [&](std::size_t i) { r[i] = returns(days[i % D], u.locate[i % D].at(u.names[i / D]), step); });

    std::vector<Result> res(S);
    parallel_for(S, [&](std::size_t s) {
        Result& o = res[s];
        std::vector<double> tr;
        std::vector<std::vector<double>> rv_train;
        for (std::size_t d = 0; d < T; ++d) {
            const auto& x = r[s * D + d];
            tr.insert(tr.end(), x.begin(), x.end());
            rv_train.push_back(minute_rv(x, per_min));
        }
        o.n = vol::fit_garch(tr, false);
        o.t = vol::fit_garch(tr, true);
        const vol::Har har = vol::fit_har(rv_train);
        if (!o.n.ok || !o.t.ok || !har.ok()) return;
        const double w = -std::expm1(-static_cast<double>(interval) / kEwmaTau), v0 = vol::sample_variance(tr);
        std::vector<double> le, lt, me, mh, mg;
        double ll_t = 0, ll_n = 0;
        std::size_t n_obs = 0;
        for (std::size_t d = T; d < D; ++d) {
            const auto& x = r[s * D + d];
            const auto ht = vol::garch_filter(x, o.t.omega, o.t.alpha, o.t.beta, o.t.long_run());
            ll_t += vol::garch_loglik(x, o.t.omega, o.t.alpha, o.t.beta, o.t.nu, o.t.long_run());
            ll_n += vol::garch_loglik(x, o.n.omega, o.n.alpha, o.n.beta, 0, o.n.long_run());
            n_obs += x.size();
            std::vector<double> e(x.size() + 1);
            e[0] = v0;
            for (std::size_t k = 0; k < x.size(); ++k) e[k + 1] = e[k] + w * (x[k] * x[k] - e[k]);
            for (std::size_t k = 0; k < x.size(); ++k)
                le.push_back(vol::qlike(x[k] * x[k], std::max(e[k], 1e-12))), lt.push_back(vol::qlike(x[k] * x[k], std::max(ht[k], 1e-12)));
            // Minute horizon, from the start of each minute with a full HAR history.
            const auto rv = minute_rv(x, per_min);
            for (std::size_t m = har.w3; m < rv.size(); ++m) {
                const std::size_t at = m * per_min;
                const double fh = har.forecast(rv, m - 1), fg = vol::variance_sum(o.t, ht[at], per_min);
                me.push_back(vol::qlike(rv[m], std::max(e[at] * static_cast<double>(per_min), 1e-12)));
                mh.push_back(vol::qlike(rv[m], std::max(fh, 1e-12)));
                mg.push_back(vol::qlike(rv[m], std::max(fg, 1e-12)));
            }
        }
        o.ll_gain = n_obs ? (ll_t - ll_n) / static_cast<double>(n_obs) : NAN;
        o.d_int = mean_diff(le, lt), o.dm_int = vol::diebold_mariano(le, lt, per_min);
        o.d_har = mean_diff(me, mh), o.dm_har = vol::diebold_mariano(me, mh, 5);
        o.d_gm = mean_diff(me, mg), o.dm_gm = vol::diebold_mariano(me, mg, 5);
    });

    std::printf("# Volatility forecasts\n\nTrain %s", app::day_of(train[0]).c_str());
    for (std::size_t d = 1; d < T; ++d) std::printf(", %s", app::day_of(train[d]).c_str());
    std::printf("; test %s", app::day_of(test[0]).c_str());
    for (std::size_t d = 1; d < test.size(); ++d) std::printf(", %s", app::day_of(test[d]).c_str());
    std::printf(". Mid changes in ticks over %" PRIu64 " s from 09:35 to 15:55 ET. GARCH(1,1) by maximum likelihood on the "
                "train days; HAR on per-minute realized variance (lags 1, 5 and 30 minutes); both scored unchanged on the test "
                "days against the market makers' EWMA (60 s). Gain: mean QLIKE of the EWMA less the model's, positive when the "
                "model forecasts better; DM: Diebold-Mariano t statistic of that difference (Newey-West). Method: docs/METHODS.md.\n\n",
                interval);
    std::printf("| Symbol | alpha | beta | half-life (s) | nu | t vs normal, test log-lik per interval | Interval: GARCH-t gain (DM) "
                "| Minute: HAR gain (DM) | Minute: GARCH-t gain (DM) |\n|---|---|---|---|---|---|---|---|---|\n");
    std::vector<double> c_int, c_har, c_gm, c_ll;
    for (std::size_t s = 0; s < S; ++s) {
        const Result& o = res[s];
        if (!o.t.ok) {
            std::printf("| %s | no fit (too few mid changes) | | | | | | | |\n", u.names[s].c_str());
            continue;
        }
        std::printf("| %s | %.3f | %.3f | %.0f | %.1f | %+.4f | %+.4f (%.1f) | %+.4f (%.1f) | %+.4f (%.1f) |\n", u.names[s].c_str(), o.t.alpha,
                    o.t.beta, o.t.half_life() * static_cast<double>(interval), o.t.nu, o.ll_gain, o.d_int, o.dm_int, o.d_har, o.dm_har,
                    o.d_gm, o.dm_gm);
        c_int.push_back(o.d_int), c_har.push_back(o.d_har), c_gm.push_back(o.d_gm), c_ll.push_back(o.ll_gain);
    }
    using strategy::micro::median;
    std::printf("| median | | | | | %+.4f | %+.4f | %+.4f | %+.4f |\n", median(c_ll), median(c_int), median(c_har), median(c_gm));
    std::printf("\n## Keys for avellaneda_stoikov\n\nThe Student-t fit, in ticks squared per %" PRIu64 " s interval:\n\n", interval);
    for (std::size_t s = 0; s < S; ++s)
        if (res[s].t.ok)
            std::printf("    %s: garch=1 garch_omega=%.6g garch_alpha=%.4f garch_beta=%.4f garch_dt_s=%" PRIu64 "\n", u.names[s].c_str(),
                        res[s].t.omega, res[s].t.alpha, res[s].t.beta, interval);
    return 0;
}
