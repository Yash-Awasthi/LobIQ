// Market realism scorecard (src/strategy/realism.hpp): the order flow statistics LOB-Bench compares,
// for a reference symbol-day A and another B (a synthetic day from gen_day, another day, another
// symbol), with the total variation and normalised Wasserstein distance of each, and a few stylised
// facts. 09:35 to 15:55 ET. Prints Markdown.
// Usage: itch_realism <store-A> <SYMBOL-A> <store-B> <SYMBOL-B>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "book/tick_book.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"
#include "strategy/microstructure.hpp"
#include "strategy/realism.hpp"

namespace {

using namespace hft;
namespace rl = strategy::realism;

constexpr std::uint64_t kNsPerS = 1'000'000'000ull;
constexpr std::uint64_t kOpen = (9 * 3600 + 35 * 60) * kNsPerS, kClose = (15 * 3600 + 55 * 60) * kNsPerS;
constexpr std::size_t kSeconds = (kClose - kOpen) / kNsPerS;

struct Stats {
    rl::Reservoir spread, imbalance, gap, life, bid_vol, ask_vol, limit_depth, cancel_depth;
    std::vector<double> volume = std::vector<double>(kSeconds, 0), ofi = std::vector<double>(kSeconds, 0), ret;
    std::uint64_t msgs = 0, adds = 0, cancels = 0, execs = 0;
};

// Applies messages to the book and records what each one shows.
struct Collector {
    Collector(book::TickBook& book, Stats& stats) : b(book), s(stats) {}
    book::TickBook& b;
    Stats& s;
    std::uint64_t ts = 0, seq = 0;
    bool in = false;  // inside the window
    struct Order {
        std::uint64_t ts;
        std::uint32_t price;
        bool buy;
    };
    std::unordered_map<std::uint64_t, Order> orders;

    double mid() const {
        const book::Bbo q = b.bbo();
        return q.bid_px && q.ask_px ? (static_cast<double>(q.bid_px) + q.ask_px) / 2 : NAN;
    }
    std::size_t second() const { return std::min<std::size_t>((ts - kOpen) / kNsPerS, kSeconds - 1); }

    void operator()(const itch::AddOrder& m) {
        const bool buy = m.side == 'B';
        if (in) {
            const double md = mid();
            if (!std::isnan(md)) s.limit_depth.add((buy ? md - m.price : m.price - md) / 100);
            ++s.adds;
        }
        b.add(m.ref, buy ? book::Side::Buy : book::Side::Sell, m.shares, m.price, seq);
        orders[m.ref] = {ts, m.price, buy};
    }
    void exec(std::uint64_t ref, std::uint32_t shares) {
        b.execute(ref, shares);
        if (in) s.volume[second()] += shares, ++s.execs;
    }
    void operator()(const itch::OrderExecuted& m) { exec(m.ref, m.shares); }
    void operator()(const itch::OrderExecutedPrice& m) { exec(m.ref, m.shares); }
    void operator()(const itch::OrderCancel& m) {
        b.cancel(m.ref, m.cancelled);
        s.cancels += in;
    }
    void operator()(const itch::OrderDelete& m) {
        const auto it = orders.find(m.ref);
        if (in && it != orders.end()) {
            const double md = mid();
            const Order& o = it->second;
            s.life.add(std::log10(static_cast<double>(ts - o.ts) + 1));
            if (!std::isnan(md)) s.cancel_depth.add((o.buy ? md - o.price : o.price - md) / 100);
            ++s.cancels;
        }
        if (it != orders.end()) orders.erase(it);
        b.erase(m.ref);
    }
    void operator()(const itch::OrderReplace& m) {
        const auto it = orders.find(m.orig_ref);
        const bool buy = it != orders.end() && it->second.buy;
        if (it != orders.end()) orders.erase(it);
        b.replace(m.orig_ref, m.new_ref, m.shares, m.price, seq);
        orders[m.new_ref] = {ts, m.price, buy};
    }
    void operator()(const itch::Trade& m) {
        if (in) s.volume[second()] += m.shares;
    }
    template <class T>
    void operator()(const T&) {}
};

Stats collect(const std::filesystem::path& dir, const std::string& sym) {
    const auto names = data::read_symbols(dir);
    const auto it = names.find(sym);
    if (it == names.end()) throw std::runtime_error("symbol " + sym + " not in " + dir.string());
    Stats s;
    book::TickBook b;
    Collector c{b, s};
    data::SymbolReader rd(dir, it->second);
    data::Record rec{};
    std::uint64_t last = 0;
    book::Bbo prev{};
    double minute_mid = NAN;
    std::size_t minute = 0;
    while (rd.next(rec)) {
        c.ts = itch::detail::read_header(rec.data).timestamp;
        if (c.ts >= kClose) break;
        c.in = c.ts >= kOpen;
        c.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, c);
        if (!c.in) continue;
        ++s.msgs;
        if (last) s.gap.add(std::log10(static_cast<double>(c.ts - last) + 1));
        last = c.ts;
        const book::Bbo q = b.bbo();
        if (!q.bid_px || !q.ask_px || q.ask_px <= q.bid_px) continue;
        s.spread.add((q.ask_px - q.bid_px) / 100.0);
        s.imbalance.add(static_cast<double>(q.bid_qty) / static_cast<double>(q.bid_qty + q.ask_qty));
        s.bid_vol.add(std::log10(1.0 + q.bid_qty)), s.ask_vol.add(std::log10(1.0 + q.ask_qty));
        if (prev.bid_px && prev.ask_px) s.ofi[c.second()] += static_cast<double>(strategy::micro::ofi_event(prev, q));
        prev = q;
        const std::size_t mn = (c.ts - kOpen) / (60 * kNsPerS);
        const double md = (static_cast<double>(q.bid_px) + q.ask_px) / 2;
        if (std::isnan(minute_mid)) minute_mid = md, minute = mn;
        while (mn > minute) s.ret.push_back(std::log(md / minute_mid)), minute_mid = md, ++minute;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) return std::fprintf(stderr, "usage: itch_realism <store-A> <SYMBOL-A> <store-B> <SYMBOL-B>\n"), 2;
    const std::filesystem::path da = argv[1], db = argv[3];
    if (!std::filesystem::is_directory(da) || !std::filesystem::is_directory(db))
        return std::fprintf(stderr, "usage: itch_realism <store-A> <SYMBOL-A> <store-B> <SYMBOL-B>\n"), 2;
    Stats a, b;
    try {
        a = collect(da, argv[2]), b = collect(db, argv[4]);
    } catch (const std::exception& e) {
        return std::fprintf(stderr, "%s\n", e.what()), 2;
    }
    const std::string an = std::string(argv[2]) + " " + da.filename().string(), bn = std::string(argv[4]) + " " + db.filename().string();
    std::printf("# Market realism: %s against %s\n\n", bn.c_str(), an.c_str());
    std::printf("The order flow statistics LOB-Bench compares, 09:35 to 15:55 ET. TV: total variation between histograms (0 the "
                "same, 1 disjoint); W1: Wasserstein distance in units of A's standard deviation. Method: docs/METHODS.md.\n\n");
    std::printf("| Statistic | A median | B median | TV | W1 |\n|---|---|---|---|---|\n");
    double tv_sum = 0;
    int tv_n = 0;
    auto row = [&](const char* name, std::vector<double> x, std::vector<double> y) {
        std::sort(x.begin(), x.end()), std::sort(y.begin(), y.end());
        const double tv = rl::total_variation(x, y), w = rl::wasserstein(x, y);
        if (!std::isnan(tv)) tv_sum += tv, ++tv_n;
        std::printf("| %s | %.3g | %.3g | %.3f | %.3f |\n", name, rl::quantile(x, 0.5), rl::quantile(y, 0.5), tv, w);
    };
    row("Spread (ticks)", a.spread.v, b.spread.v);
    row("Best-level imbalance, bid share", a.imbalance.v, b.imbalance.v);
    row("Time between messages (log10 ns)", a.gap.v, b.gap.v);
    row("Time to cancel (log10 ns)", a.life.v, b.life.v);
    row("Best bid size (log10 shares)", a.bid_vol.v, b.bid_vol.v);
    row("Best ask size (log10 shares)", a.ask_vol.v, b.ask_vol.v);
    row("Limit order depth (ticks from mid)", a.limit_depth.v, b.limit_depth.v);
    row("Cancellation depth (ticks from mid)", a.cancel_depth.v, b.cancel_depth.v);
    auto per_minute = [](std::vector<double> v) {
        for (double& x : v) x *= 60;
        return v;
    };
    row("Traded volume per minute (1 s buckets)", per_minute(a.volume), per_minute(b.volume));
    row("Order flow imbalance per second (shares)", a.ofi, b.ofi);
    std::printf("| **mean** | | | **%.3f** | |\n\n", tv_n ? tv_sum / tv_n : NAN);

    auto abs_acf = [](const std::vector<double>& r) {
        std::vector<double> x;
        for (const double v : r) x.push_back(std::fabs(v));
        double s = 0;
        for (std::size_t l = 1; l <= 5; ++l) s += rl::acf(x, l);
        return s / 5;
    };
    auto ratio = [](std::uint64_t n, std::uint64_t d) { return d ? static_cast<double>(n) / static_cast<double>(d) : NAN; };
    std::printf("## Stylised facts\n\n| Fact | A | B |\n|---|---|---|\n");
    std::printf("| Messages per minute | %.0f | %.0f |\n", a.msgs / (kSeconds / 60.0), b.msgs / (kSeconds / 60.0));
    std::printf("| Cancellations per new order | %.2f | %.2f |\n", ratio(a.cancels, a.adds), ratio(b.cancels, b.adds));
    std::printf("| Executions per new order | %.3f | %.3f |\n", ratio(a.execs, a.adds), ratio(b.execs, b.adds));
    std::printf("| Kurtosis of 1-minute returns (normal: 3) | %.1f | %.1f |\n", rl::kurtosis(a.ret), rl::kurtosis(b.ret));
    std::printf("| Autocorrelation of 1-minute returns, lag 1 | %.3f | %.3f |\n", rl::acf(a.ret, 1), rl::acf(b.ret, 1));
    std::printf("| Volatility clustering: autocorrelation of |returns|, lags 1-5 | %.3f | %.3f |\n", abs_acf(a.ret), abs_acf(b.ret));
    return 0;
}
