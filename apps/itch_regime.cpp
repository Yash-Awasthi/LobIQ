// Market regimes, walk-forward (src/strategy/regime.hpp): per symbol-minute features (realized
// variance, spread, trade count, order flow imbalance), standardised per symbol on the train days; one
// Gaussian HMM over every train symbol-day, its number of states chosen by BIC unless given. Test days
// are labelled causally (forward filter), and each regime is described by what came next: the next
// minute's realized variance and absolute mid change. With --pnl, a backtest's minute PnL on a test
// day is split by the regime it was earned in. Prints Markdown.
// Usage: itch_regime --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--states K]
//                    [--out FILE.csv] [--pnl SYMBOL:pnl_minutes.csv]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "book/tick_book.hpp"
#include "core/parallel.hpp"
#include "data/store.hpp"
#include "days.hpp"
#include "feed/itch.hpp"
#include "strategy/market_feed.hpp"
#include "strategy/microstructure.hpp"
#include "strategy/regime.hpp"

namespace {

using namespace hft;
namespace fs = std::filesystem;
namespace rg = strategy::regime;

constexpr std::uint64_t kSec = 1'000'000'000ull, kMin = 60 * kSec;
constexpr std::uint64_t kOpen = (9 * 3600 + 35 * 60) * kSec, kClose = (15 * 3600 + 55 * 60) * kSec;
constexpr std::size_t kMinutes = (kClose - kOpen) / kMin, kF = 4;
constexpr std::array<const char*, kF> kFeature = {"log RV", "log spread", "log trades", "log OFI/depth"};

struct Minute {
    double rv = 0, spread_sum = 0, spread_n = 0, trades = 0, ofi = 0, depth_sum = 0, depth_n = 0, dmid = 0;
    rg::Row row() const {
        const double spread = spread_n > 0 ? spread_sum / spread_n : 1, depth = depth_n > 0 ? depth_sum / depth_n : 1;
        return {std::log(rv + 1e-4), std::log(std::max(spread, 0.5)), std::log1p(trades), std::log1p(std::abs(ofi) / std::max(depth, 1.0))};
    }
};

// Mid sampled each second for realized variance and spread, in penny ticks; OFI and trades per event.
std::vector<Minute> replay(const fs::path& dir, std::uint16_t loc) {
    std::vector<Minute> out(kMinutes);
    book::TickBook b;
    strategy::MarketFeed<> feed(b);
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    strategy::MarketEvent e;
    book::Bbo prev{};
    double last_mid = NAN, sec_mid = NAN, open_mid = NAN;
    std::uint64_t next_sec = kOpen;
    std::size_t cur = 0;
    auto sample = [&](std::uint64_t upto) {
        while (next_sec <= upto && next_sec < kClose) {
            const std::size_t m = (next_sec - kOpen) / kMin;
            if (std::isfinite(last_mid)) {
                if (std::isfinite(sec_mid)) out[m].rv += (last_mid - sec_mid) * (last_mid - sec_mid);
                const book::Bbo q = b.bbo();
                if (q.bid_px && q.ask_px > q.bid_px) out[m].spread_sum += (q.ask_px - q.bid_px) / 100.0, out[m].spread_n += 1;
                if (m != cur) {
                    if (std::isfinite(open_mid)) out[cur].dmid = last_mid - open_mid;
                    open_mid = last_mid, cur = m;
                }
                if (!std::isfinite(open_mid)) open_mid = last_mid;
            }
            sec_mid = last_mid;
            next_sec += kSec;
        }
    };
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        if (ts >= kOpen) sample(ts);
        if (ts >= kClose) break;
        if (!feed.on_itch(rec.data, rec.len, rec.seq, e)) continue;
        const book::Bbo q = b.bbo();
        if (q.bid_px && q.ask_px > q.bid_px) last_mid = (static_cast<double>(q.bid_px) + q.ask_px) / 200.0;
        if (ts >= kOpen) {
            Minute& m = out[(ts - kOpen) / kMin];
            if (e.kind == strategy::EventKind::Execute || e.kind == strategy::EventKind::Hidden) m.trades += 1;
            if (prev.bid_px && q.bid_px) {
                m.ofi += static_cast<double>(strategy::micro::ofi_event(prev, q));
                m.depth_sum += 0.5 * static_cast<double>(q.bid_qty + q.ask_qty), m.depth_n += 1;
            }
        }
        prev = q;
    }
    sample(kClose);
    if (std::isfinite(open_mid) && std::isfinite(last_mid)) out[cur].dmid = last_mid - open_mid;
    return out;
}

// minute index (from 09:30) -> cumulative total USD, tab- or comma-separated with a header.
std::vector<double> read_pnl(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::vector<double> v;
    std::getline(f, line);
    while (std::getline(f, line)) {
        const auto at = line.find_first_of("\t,");
        if (at != std::string::npos) v.push_back(std::atof(line.c_str() + at + 1));
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage =
        "usage: itch_regime --train <store>[,..] --test <store>[,..] [--symbols N|SYM,..] [--states K] [--out FILE.csv] "
        "[--pnl SYMBOL:pnl_minutes.csv]";
    std::vector<fs::path> train, test;
    std::string symbols = "10", out_path, pnl_arg;
    std::size_t states = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return std::fprintf(stderr, "missing value for %s\n%s\n", a.c_str(), usage), 2;
        const std::string v = argv[++i];
        if (a == "--train") train = app::paths(v);
        else if (a == "--test") test = app::paths(v);
        else if (a == "--symbols") symbols = v;
        else if (a == "--states") states = std::strtoul(v.c_str(), nullptr, 10);
        else if (a == "--out") out_path = v;
        else if (a == "--pnl") pnl_arg = v;
        else return std::fprintf(stderr, "unknown option %s\n%s\n", a.c_str(), usage), 2;
    }
    if (train.empty() || test.empty() || states == 1 || states > 6) return std::fprintf(stderr, "%s\n", usage), 2;
    std::vector<fs::path> days = train;
    days.insert(days.end(), test.begin(), test.end());
    for (const auto& d : days)
        if (!fs::is_directory(d)) return std::fprintf(stderr, "not a store: %s\n", d.c_str()), 2;
    const app::Universe u = app::universe(days, symbols);
    if (!u.error.empty()) return std::fprintf(stderr, "%s\n", u.error.c_str()), 2;
    const std::size_t S = u.names.size(), D = days.size(), T = train.size();

    std::vector<std::vector<Minute>> mins(S * D);
    parallel_for(S * D, [&](std::size_t i) { mins[i] = replay(days[i % D], u.locate[i % D].at(u.names[i / D])); });

    // Standardised per symbol from its train minutes; one model over every symbol.
    std::vector<rg::Seq> seqs(S * D);
    std::vector<rg::Standardizer> z(S);
    for (std::size_t s = 0; s < S; ++s) {
        for (std::size_t d = 0; d < D; ++d)
            for (const Minute& m : mins[s * D + d]) seqs[s * D + d].push_back(m.row());
        std::vector<const rg::Row*> tr;
        for (std::size_t d = 0; d < T; ++d)
            for (const rg::Row& r : seqs[s * D + d]) tr.push_back(&r);
        z[s].fit(tr);
        for (std::size_t d = 0; d < D; ++d)
            for (rg::Row& r : seqs[s * D + d]) r = z[s].apply(r);
    }
    std::vector<rg::Seq> train_seqs;
    std::size_t n_train = 0;
    for (std::size_t s = 0; s < S; ++s)
        for (std::size_t d = 0; d < T; ++d) train_seqs.push_back(seqs[s * D + d]), n_train += seqs[s * D + d].size();
    std::vector<std::pair<std::size_t, double>> bic;
    rg::Hmm h;
    double best_bic = INFINITY;
    for (std::size_t k = states ? states : 2; k <= (states ? states : 4); ++k) {
        rg::Hmm c = rg::fit_hmm(train_seqs, k);
        if (!c.ok) continue;
        const double b = -2 * c.loglik + c.params() * std::log(static_cast<double>(n_train));
        bic.emplace_back(k, b);
        if (b < best_bic) best_bic = b, h = std::move(c);
    }
    if (!h.ok) return std::fprintf(stderr, "no regime model: too few minutes\n"), 1;
    rg::sort_states(h, 0);
    const std::size_t K = h.K;

    // Test days: causal labels, and what followed each labelled minute.
    std::vector<double> cnt(K, 0), stay(K, 0), moves(K, 0), next_rv(K, 0), next_abs(K, 0), nxt(K, 0);
    std::vector<std::array<double, kF>> raw(K);
    std::vector<std::vector<std::size_t>> labels(S * D);
    for (std::size_t s = 0; s < S; ++s)
        for (std::size_t d = T; d < D; ++d) {
            const auto p = rg::filter(h, seqs[s * D + d]);
            auto& lab = labels[s * D + d];
            for (const auto& row : p) lab.push_back(static_cast<std::size_t>(std::max_element(row.begin(), row.end()) - row.begin()));
            const auto& m = mins[s * D + d];
            for (std::size_t t = 0; t < lab.size(); ++t) {
                const std::size_t k = lab[t];
                cnt[k] += 1;
                const rg::Row r = m[t].row();
                for (std::size_t f = 0; f < kF; ++f) raw[k][f] += r[f];
                if (t + 1 < lab.size()) {
                    moves[k] += 1, stay[k] += lab[t + 1] == k;
                    next_rv[k] += m[t + 1].rv, next_abs[k] += std::abs(m[t + 1].dmid), nxt[k] += 1;
                }
            }
        }

    std::printf("# Market regimes\n\n%zu symbols; train %zu days, test %zu days. Per minute from 09:35 to 15:55 ET: log realized variance "
                "of 1 s mid changes, log mean spread, log trade count, log absolute OFI over depth; standardised per symbol on the train days. "
                "One Gaussian HMM (diagonal covariances) over every train symbol-day by Baum-Welch; test minutes labelled by the forward "
                "filter, which uses nothing later. States ordered from the calmest. Method: docs/METHODS.md.\n\n",
                S, T, D - T);
    std::printf("BIC on the train days (lower is better):");
    for (const auto& [k, b] : bic) std::printf(" %zu states %.0f%s", k, b, k == K ? " (chosen)" : "");
    std::printf("\n\n## Regimes on the test days\n\n| State | share of minutes | P(same next minute) | train P(stay) |");
    for (const char* f : kFeature) std::printf(" %s |", f);
    std::printf(" next-minute RV (ticks²) | next-minute abs mid change (ticks) |\n|---|---|---|---|");
    for (std::size_t f = 0; f < kF + 2; ++f) std::printf("---|");
    std::printf("\n");
    double total = 0;
    for (const double c : cnt) total += c;
    for (std::size_t k = 0; k < K; ++k) {
        std::printf("| %zu | %.1f%% | %.3f | %.3f |", k, 100 * cnt[k] / std::max(total, 1.0), moves[k] > 0 ? stay[k] / moves[k] : NAN,
                    h.A[k * K + k]);
        for (std::size_t f = 0; f < kF; ++f) std::printf(" %.2f |", cnt[k] > 0 ? raw[k][f] / cnt[k] : NAN);
        std::printf(" %.3f | %.3f |\n", nxt[k] > 0 ? next_rv[k] / nxt[k] : NAN, nxt[k] > 0 ? next_abs[k] / nxt[k] : NAN);
    }
    std::printf("\nFeature columns are means of the raw (unstandardised) features over the minutes labelled with the state.\n");

    if (!out_path.empty()) {
        std::FILE* f = std::fopen(out_path.c_str(), "w");
        if (!f) return std::fprintf(stderr, "cannot write %s\n", out_path.c_str()), 1;
        std::fprintf(f, "symbol,day,minute,state\n");
        for (std::size_t s = 0; s < S; ++s)
            for (std::size_t d = T; d < D; ++d)
                for (std::size_t t = 0; t < labels[s * D + d].size(); ++t) {
                    const std::size_t at = 9 * 60 + 35 + t;
                    std::fprintf(f, "%s,%s,%02zu:%02zu,%zu\n", u.names[s].c_str(), app::day_of(days[d]).c_str(), at / 60, at % 60,
                                 labels[s * D + d][t]);
                }
        std::fclose(f);
    }

    if (!pnl_arg.empty()) {
        const auto colon = pnl_arg.find(':');
        const std::string sym = colon == std::string::npos ? "" : pnl_arg.substr(0, colon);
        const auto it = std::find(u.names.begin(), u.names.end(), sym);
        if (it == u.names.end()) return std::fprintf(stderr, "--pnl needs SYMBOL:FILE with a symbol of the run\n"), 2;
        const std::size_t s = static_cast<std::size_t>(it - u.names.begin());
        const auto pnl = read_pnl(pnl_arg.substr(colon + 1));
        if (pnl.size() < 6) return std::fprintf(stderr, "no minute PnL in %s\n", pnl_arg.substr(colon + 1).c_str()), 2;
        const auto& lab = labels[s * D + T];  // the first test day
        std::vector<double> sum(K, 0), n(K, 0);
        for (std::size_t t = 0; t < lab.size(); ++t) {
            const std::size_t i = t + 5;  // backtest minutes count from 09:30
            if (i >= pnl.size() || i == 0) continue;
            sum[lab[t]] += pnl[i] - pnl[i - 1], n[lab[t]] += 1;
        }
        std::printf("\n## Backtest PnL by regime: %s on %s\n\n| State | minutes | PnL (USD) | per minute (USD) |\n|---|---|---|---|\n",
                    sym.c_str(), app::day_of(days[T]).c_str());
        for (std::size_t k = 0; k < K; ++k)
            std::printf("| %zu | %.0f | %.2f | %.3f |\n", k, n[k], sum[k], n[k] > 0 ? sum[k] / n[k] : NAN);
    }
    return 0;
}
