// Multi-level order flow imbalance and cross-impact on one stored day (src/strategy/mlofi.hpp,
// Cont, Cucuringu and Zhang 2023): per symbol, how much of each interval's mid change the best-level
// OFI and the integrated OFI explain out of sample, and whether other symbols' lagged OFI helps
// forecast the next interval. Fitted before --split, scored after. Prints Markdown.
// Usage: itch_mlofi <store> <SYMBOL,SYMBOL,...> [--interval SECONDS] [--split HH:MM]

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "core/parallel.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"
#include "strategy/microstructure.hpp"
#include "strategy/mlofi.hpp"

namespace {

using namespace hft;
namespace ml = strategy::mlofi;

constexpr std::uint64_t kNsPerS = 1'000'000'000ull;
constexpr std::uint64_t kOpen = (9 * 3600 + 35 * 60) * kNsPerS, kClose = (15 * 3600 + 55 * 60) * kNsPerS;

struct Interval {
    std::array<double, ml::kLevels> ofi{};  // shares, then divided by the interval's mean depth
    double depth_sum = 0, dmid = 0;          // mid change in ticks
    std::uint64_t events = 0;
};

// One symbol's day on the interval grid.
std::vector<Interval> replay(const std::filesystem::path& dir, std::uint16_t loc, std::uint64_t step) {
    std::vector<Interval> out((kClose - kOpen) / step);
    book::TickBook b;
    book::ItchApply<book::TickBook> ap{b};
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    ml::Levels prev{}, cur{};
    bool have = false;
    double mid = NAN;
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        ap.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, ap);
        if (ts < kOpen) continue;
        if (ts >= kClose) break;
        ml::snapshot(b, book::Side::Buy, book::Side::Sell, cur);
        Interval& iv = out[std::min<std::size_t>((ts - kOpen) / step, out.size() - 1)];
        if (have) ml::add_ofi(prev, cur, iv.ofi);
        iv.depth_sum += ml::depth(cur), ++iv.events;
        if (cur.bid_px[0] && cur.ask_px[0] && cur.ask_px[0] > cur.bid_px[0]) {
            const double m = (static_cast<double>(cur.bid_px[0]) + cur.ask_px[0]) / 200.0;  // penny ticks
            if (!std::isnan(mid)) iv.dmid += m - mid;
            mid = m;
        }
        prev = cur, have = true;
    }
    for (Interval& iv : out)
        if (iv.events && iv.depth_sum > 0)
            for (double& x : iv.ofi) x /= iv.depth_sum / static_cast<double>(iv.events);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage = "usage: itch_mlofi <store> <SYMBOL,SYMBOL,...> [--interval SECONDS] [--split HH:MM]";
    std::uint64_t step = 10 * kNsPerS, split = (12 * 3600 + 45 * 60) * kNsPerS;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            const long s = std::atol(argv[++i]);
            if (s < 1 || s > 3600) return std::fprintf(stderr, "%s\n", usage), 2;
            step = static_cast<std::uint64_t>(s) * kNsPerS;
        } else if (std::strcmp(argv[i], "--split") == 0 && i + 1 < argc) {
            unsigned h, m;
            if (std::sscanf(argv[++i], "%2u:%2u", &h, &m) != 2 || h > 23 || m > 59) return std::fprintf(stderr, "%s\n", usage), 2;
            split = (static_cast<std::uint64_t>(h) * 3600 + m * 60) * kNsPerS;
        } else if (argv[i][0] == '-') return std::fprintf(stderr, "%s\n", usage), 2;
        else pos.push_back(argv[i]);
    }
    if (pos.size() != 2 || split <= kOpen || split >= kClose) return std::fprintf(stderr, "%s\n", usage), 2;
    const std::filesystem::path dir = pos[0];
    const auto names = data::read_symbols(dir);
    std::vector<std::string> syms;
    std::stringstream ss(pos[1]);
    for (std::string s; std::getline(ss, s, ',');)
        if (!s.empty()) syms.push_back(s);
    for (const std::string& s : syms)
        if (!names.contains(s)) return std::fprintf(stderr, "symbol %s not in %s\n", s.c_str(), dir.c_str()), 2;
    if (syms.empty()) return std::fprintf(stderr, "%s\n", usage), 2;

    std::vector<std::vector<Interval>> days(syms.size());
    hft::parallel_for(syms.size(), [&](std::size_t i) { days[i] = replay(dir, names.at(syms[i]), step); });
    const std::size_t n = days[0].size(), cut = static_cast<std::size_t>((split - kOpen) / step);

    std::printf("# Multi-level order flow imbalance: %s\n\n", dir.filename().c_str());
    std::printf("Top %d levels per side, %" PRIu64 " s intervals from 09:35 to 15:55 ET; weights and regressions fitted on the "
                "intervals before %02" PRIu64 ":%02" PRIu64 ", R² measured on the rest. Method: Cont, Cucuringu and Zhang (2023), "
                "docs/METHODS.md.\n\n",
                ml::kLevels, step / kNsPerS, split / kNsPerS / 3600, split / kNsPerS / 60 % 60);
    std::printf("## Price impact, out of sample\n\nMid change over an interval explained by that interval's OFI.\n\n"
                "| Symbol | First component's share of variance | Weights, levels 1-3 | R² best level | R² integrated |\n|---|---|---|---|---|\n");
    std::vector<std::vector<double>> integrated(syms.size(), std::vector<double>(n, 0));
    std::vector<double> r2_best, r2_int;
    for (std::size_t i = 0; i < syms.size(); ++i) {
        const auto& d = days[i];
        std::vector<std::array<double, ml::kLevels>> train;
        for (std::size_t k = 0; k < cut; ++k) train.push_back(d[k].ofi);
        double share = 0;
        const auto w = ml::first_pc(train, &share);
        for (std::size_t k = 0; k < n; ++k)
            for (int m = 0; m < ml::kLevels; ++m) integrated[i][k] += w[m] * d[k].ofi[m];
        auto oos = [&](auto feature) {
            std::vector<double> x, y, px, py;
            for (std::size_t k = 0; k < n; ++k) (k < cut ? x : px).push_back(feature(k)), (k < cut ? y : py).push_back(d[k].dmid);
            const auto f = strategy::micro::ols(x, y);
            std::vector<double> pred;
            for (const double v : px) pred.push_back(f.a + f.b * v);
            return strategy::micro::r2_of(pred, py);
        };
        const double rb = oos([&](std::size_t k) { return d[k].ofi[0]; }), ri = oos([&](std::size_t k) { return integrated[i][k]; });
        r2_best.push_back(rb), r2_int.push_back(ri);
        std::printf("| %s | %.2f | %.2f, %.2f, %.2f | %.3f | %.3f |\n", syms[i].c_str(), share, w[0], w[1], w[2], rb, ri);
    }
    std::printf("| median | | | %.3f | %.3f |\n", strategy::micro::median(r2_best), strategy::micro::median(r2_int));

    if (syms.size() < 2) {
        std::printf("\nCross-impact needs two or more symbols.\n");
        return 0;
    }
    std::printf("\n## Forecasting the next interval: own OFI against every symbol's OFI\n\n"
                "Next interval's mid change regressed (ridge) on this interval's integrated OFI, of the symbol alone or of all %zu symbols.\n\n"
                "| Symbol | R² own | R² own and others | Gain |\n|---|---|---|---|\n",
                syms.size());
    std::vector<double> own_all, cross_all;
    for (std::size_t i = 0; i < syms.size(); ++i) {
        auto oos = [&](bool cross) {
            std::vector<std::vector<double>> X, PX;
            std::vector<double> y, py;
            for (std::size_t k = 0; k + 1 < n; ++k) {
                std::vector<double> row;
                if (cross)
                    for (std::size_t j = 0; j < syms.size(); ++j) row.push_back(integrated[j][k]);
                else
                    row.push_back(integrated[i][k]);
                (k + 1 < cut ? X : PX).push_back(row), (k + 1 < cut ? y : py).push_back(days[i][k + 1].dmid);
            }
            const auto b = ml::ridge(X, y, 1.0);
            std::vector<double> pred;
            for (const auto& row : PX) pred.push_back(ml::predict(b, row));
            return strategy::micro::r2_of(pred, py);
        };
        const double o = oos(false), c = oos(true);
        own_all.push_back(o), cross_all.push_back(c);
        std::printf("| %s | %.4f | %.4f | %+.4f |\n", syms[i].c_str(), o, c, c - o);
    }
    std::printf("| median | %.4f | %.4f | |\n\nOut-of-sample R² can be negative: a forecast worse than the test period's own mean.\n",
                strategy::micro::median(own_all), strategy::micro::median(cross_all));
    return 0;
}
