// Order flow as a multivariate Hawkes process (src/strategy/hawkes_mv.hpp), walk-forward: six event
// types at the best quotes (market buys and sells, limit orders joining or improving each side,
// cancels at each side), fitted per symbol on windows of the train days and scored on the same windows
// of the test days against a Poisson process with the train rates. Prints Markdown.
// Usage: itch_hawkes --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--window MINUTES]

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "book/tick_book.hpp"
#include "core/parallel.hpp"
#include "data/store.hpp"
#include "days.hpp"
#include "feed/itch.hpp"
#include "strategy/hawkes_mv.hpp"
#include "strategy/market_feed.hpp"
#include "strategy/microstructure.hpp"

namespace {

using namespace lobiq;
namespace fs = std::filesystem;
namespace hk = strategy::hawkes;
using strategy::EventKind;

constexpr std::uint64_t kSec = 1'000'000'000ull;
constexpr std::size_t kTypes = 6, kWindows = 6;  // windows start on the hour, 10:00 to 15:00 ET
constexpr std::array<const char*, kTypes> kNames = {"MO buy", "MO sell", "LO bid", "LO ask", "cancel bid", "cancel ask"};

int type_of(const strategy::MarketEvent& e) {
    const bool bid = e.side == book::Side::Buy;
    switch (e.kind) {
        case EventKind::Execute: return bid ? 1 : 0;  // a resting bid hit is a market sell
        case EventKind::Add: return e.at_best ? (bid ? 2 : 3) : -1;
        case EventKind::Cancel:
        case EventKind::Delete: return e.at_best ? (bid ? 4 : 5) : -1;
        default: return -1;
    }
}

// One realization per window. Events of one type at one timestamp (one order sweeping several
// resting orders, or a batch of cancels) count once.
std::vector<hk::Realization> windows(const fs::path& dir, std::uint16_t loc, std::uint64_t width) {
    std::vector<hk::Realization> out(kWindows);
    for (auto& w : out) w.horizon = static_cast<double>(width) / kSec;
    book::TickBook b;
    strategy::MarketFeed<> feed(b);
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    strategy::MarketEvent e;
    std::array<std::uint64_t, kTypes> last{};
    const std::uint64_t first = 10 * 3600 * kSec, last_end = first + (kWindows - 1) * 3600 * kSec + width;
    while (rd.next(rec)) {
        if (!feed.on_itch(rec.data, rec.len, rec.seq, e)) continue;
        if (e.ts >= last_end) break;
        if (e.ts < first) continue;
        const std::uint64_t w = (e.ts - first) / (3600 * kSec), start = first + w * 3600 * kSec;
        const int t = type_of(e);
        if (t < 0 || e.ts >= start + width) continue;
        const auto k = static_cast<std::size_t>(t);
        if (last[k] == e.ts) continue;
        last[k] = e.ts;
        out[w].t.push_back(static_cast<double>(e.ts - start) / kSec);
        out[w].type.push_back(static_cast<std::uint8_t>(t));
    }
    return out;
}

struct Result {
    hk::Model m;
    double rate = 0, radius = NAN, own = NAN, gain = NAN, ks_train = NAN, ks_test = NAN;
};

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage = "usage: itch_hawkes --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--window MINUTES (1-60)]";
    std::vector<fs::path> train, test;
    std::string symbols = "10";
    std::uint64_t minutes = 10;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return std::fprintf(stderr, "missing value for %s\n%s\n", a.c_str(), usage), 2;
        const std::string v = argv[++i];
        if (a == "--train") train = app::paths(v);
        else if (a == "--test") test = app::paths(v);
        else if (a == "--symbols") symbols = v;
        else if (a == "--window") minutes = std::strtoull(v.c_str(), nullptr, 10);
        else return std::fprintf(stderr, "unknown option %s\n%s\n", a.c_str(), usage), 2;
    }
    if (train.empty() || test.empty() || minutes < 1 || minutes > 60) return std::fprintf(stderr, "%s\n", usage), 2;
    std::vector<fs::path> days = train;
    days.insert(days.end(), test.begin(), test.end());
    for (const auto& d : days)
        if (!fs::is_directory(d)) return std::fprintf(stderr, "not a store: %s\n", d.c_str()), 2;
    const app::Universe u = app::universe(days, symbols);
    if (!u.error.empty()) return std::fprintf(stderr, "%s\n", u.error.c_str()), 2;
    const std::size_t S = u.names.size(), D = days.size(), T = train.size();

    std::vector<std::vector<hk::Realization>> ev(S * D);
    parallel_for(S * D, [&](std::size_t i) { ev[i] = windows(days[i % D], u.locate[i % D].at(u.names[i / D]), minutes * 60 * kSec); });

    std::vector<Result> res(S);
    parallel_for(S, [&](std::size_t s) {
        std::vector<hk::Realization> tr, te;
        for (std::size_t d = 0; d < D; ++d)
            for (const auto& w : ev[s * D + d]) (d < T ? tr : te).push_back(w);
        Result& o = res[s];
        o.m = hk::fit(tr, kTypes, {0.1, 1, 10, 100, 1000});
        if (!o.m.ok) return;
        double n = 0, horizon = 0, diag = 0, off = 0;
        std::array<double, kTypes> cnt{};
        for (const auto& w : tr) {
            horizon += w.horizon;
            for (const auto t : w.type) cnt[t] += 1, n += 1;
        }
        o.rate = n / horizon;
        o.radius = hk::radius(o.m);
        for (std::size_t i = 0; i < kTypes; ++i)
            for (std::size_t j = 0; j < kTypes; ++j) (i == j ? diag : off) += o.m.a(i, j);
        o.own = diag + off > 0 ? diag / (diag + off) : NAN;
        hk::Model poisson;
        poisson.D = kTypes, poisson.beta = 1, poisson.alpha.assign(kTypes * kTypes, 0);
        for (const double c : cnt) poisson.mu.push_back(std::max(c, 1.0) / horizon);
        double ne = 0;
        for (const auto& w : te) ne += static_cast<double>(w.t.size());
        if (ne > 0) o.gain = (hk::loglik(te, o.m) - hk::loglik(te, poisson)) / ne;
        o.ks_train = hk::rescaling_ks(tr, o.m);
        o.ks_test = hk::rescaling_ks(te, o.m);
    });

    std::printf("# Order flow as a multivariate Hawkes process\n\n");
    std::printf("%zu train and %zu test days; per day %zu windows of %" PRIu64 " minutes starting on the hour from 10:00 ET. Six event types at "
                "the best quotes; a kernel a_ij beta exp(-beta t) per pair with one decay beta, fitted by EM on the train windows "
                "(docs/METHODS.md). Branching: spectral radius of the matrix a, the share of events triggered by earlier events. "
                "Gain: test log-likelihood per event over a Poisson process with the train rates. KS: distance of the time-rescaled "
                "inter-event times from Exp(1), 0 for a perfect model.\n\n",
                T, D - T, kWindows, minutes);
    std::printf("| Symbol | events/s | beta (1/s) | branching | own-type share | test gain (nats/event) | KS train | KS test |\n"
                "|---|---|---|---|---|---|---|---|\n");
    std::vector<double> br, gain, kst;
    std::vector<std::vector<double>> a(kTypes * kTypes);
    for (std::size_t s = 0; s < S; ++s) {
        const Result& o = res[s];
        if (!o.m.ok) {
            std::printf("| %s | too few events | | | | | | |\n", u.names[s].c_str());
            continue;
        }
        std::printf("| %s | %.1f | %.1f | %.3f | %.2f | %+.3f | %.3f | %.3f |\n", u.names[s].c_str(), o.rate, o.m.beta, o.radius, o.own,
                    o.gain, o.ks_train, o.ks_test);
        br.push_back(o.radius), gain.push_back(o.gain), kst.push_back(o.ks_test);
        for (std::size_t k = 0; k < kTypes * kTypes; ++k) a[k].push_back(o.m.alpha[k]);
    }
    using strategy::micro::median;
    if (!br.empty()) std::printf("| median | | | %.3f | | %+.3f | | %.3f |\n", median(br), median(gain), median(kst));
    if (br.empty()) return 0;
    std::printf("\n## Who triggers whom\n\nMedian over symbols of a_ij: expected events of the row type triggered by one event of the "
                "column type.\n\n| triggered \\ by |");
    for (const char* n : kNames) std::printf(" %s |", n);
    std::printf("\n|---|");
    for (std::size_t j = 0; j < kTypes; ++j) std::printf("---|");
    std::printf("\n");
    for (std::size_t i = 0; i < kTypes; ++i) {
        std::printf("| %s |", kNames[i]);
        for (std::size_t j = 0; j < kTypes; ++j) std::printf(" %.3f |", median(a[i * kTypes + j]));
        std::printf("\n");
    }
    return 0;
}
