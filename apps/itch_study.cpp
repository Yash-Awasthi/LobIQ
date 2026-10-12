// Microstructure study on stored Nasdaq days: the order flow imbalance model of Cont, Kukanov
// and Stoikov (2014) and Stoikov's micro-price (2018), fitted on train days, scored on later
// test days. Prints a Markdown report.
// Usage: itch_study --train <store>[,<store>..] --test <store>[,<store>..] [--symbols N]
// Env: THREADS (default: all CPUs).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"
#include "strategy/microstructure.hpp"

namespace {

using namespace hft;
namespace fs = std::filesystem;

constexpr std::uint64_t kSec = 1'000'000'000;
constexpr std::uint64_t kStart = 10 * 3600 * kSec;          // 10:00 ET: the open's auction burst is over
constexpr std::uint64_t kEnd = (15 * 3600 + 30 * 60) * kSec;  // 15:30 ET: before the closing cross
constexpr std::uint64_t kInterval = 10 * kSec;               // OFI aggregation interval (as in the paper)
constexpr std::uint64_t kWindow = 30 * 60 * kSec;            // half-hour regression windows
constexpr std::uint64_t kHorizon = kSec;                     // time target for the micro-price
using namespace strategy::micro;

struct Interval {
    double ofi = 0, dmid = 0, depth = 0;  // shares, ticks, mean (bid + ask size) / 2
    std::uint32_t window = 0;
};

struct Errors {
    // Squared error in ticks^2 against the mid at the next mid change [0] and after kHorizon [1],
    // for the mid, the size-weighted mid and the micro-price.
    std::array<std::array<double, 3>, 2> se{};
    std::array<std::uint64_t, 2> n{};
    void add(const Errors& o) {
        for (int k = 0; k < 2; ++k) {
            n[k] += o.n[k];
            for (int p = 0; p < 3; ++p) se[k][p] += o.se[k][p];
        }
    }
};

// Orders joining the best quote at a 1-tick spread, by where they join (Moallemi and Yuan 2017):
// bucket 0 opens a new best level, 1-5 join behind 0-0.25, 0.25-0.5, 0.5-1, 1-2, 2+ times the
// typical level size.
struct Queue {
    static constexpr int kPos = 6;
    std::array<double, kPos> adds{}, fills{}, wait_s{}, markout{}, marked{};
    void add(const Queue& o) {
        for (std::size_t i = 0; i < kPos; ++i)
            adds[i] += o.adds[i], fills[i] += o.fills[i], wait_s[i] += o.wait_s[i], markout[i] += o.markout[i], marked[i] += o.marked[i];
    }
};
inline int queue_bucket(double ahead, double depth) {
    if (ahead <= 0) return 0;
    const double r = depth > 0 ? ahead / depth : 1e9;
    return r < 0.25 ? 1 : r < 0.5 ? 2 : r < 1 ? 3 : r < 2 ? 4 : 5;
}

struct DayResult {
    std::vector<Interval> iv;
    Counts c;
    Errors e;
    Queue q;
    std::uint64_t spread_obs[3] = {};  // 1 tick, 2 ticks, wider
};

struct Name {
    std::string s;
    void operator()(const itch::StockDirectory& m) {
        s.assign(m.stock, 8);
        s.erase(s.find_last_not_of(' ') + 1);
    }
    template <class T>
    void operator()(const T&) {}
};

std::string read_symbol(const fs::path& dir, std::uint16_t loc) {
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    Name n;
    for (int i = 0; i < 64 && n.s.empty() && rd.next(rec); ++i) itch::dispatch(rec.data, rec.len, n);
    return n.s;
}

std::vector<std::uint16_t> locates(const fs::path& dir) {
    std::vector<std::uint16_t> v;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".idx") v.push_back(static_cast<std::uint16_t>(std::stoul(e.path().stem().string())));
    std::sort(v.begin(), v.end());
    return v;
}

template <class F>
void parallel(std::size_t n, F&& f) {
    const char* te = std::getenv("THREADS");
    const unsigned threads = te ? static_cast<unsigned>(std::strtoul(te, nullptr, 10)) : std::thread::hardware_concurrency();
    std::atomic<std::size_t> next{0};
    auto worker = [&] {
        for (std::size_t i; (i = next.fetch_add(1)) < n;) f(i);
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < std::max(threads, 1u); ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

struct State {
    char trading = 0;
    void operator()(const itch::TradingAction& m) { trading = m.state; }
    template <class T>
    void operator()(const T&) {}
};

struct Live {
    int bucket;
    bool buy;
    std::uint32_t px;
    std::uint64_t ts;
};
struct Markout {
    std::uint64_t ts;
    int bucket;
    bool buy;
    double px;
};
struct QueueTrack {
    std::unordered_map<std::uint64_t, Live>& live;
    std::deque<Markout>& marks;
    Queue& q;
    const book::Bbo& bbo;
    const double& depth;
    std::uint64_t ts = 0;
    bool on = false;  // in window, trading, 1-tick spread before the message
    std::uint32_t tick = 100;
    void operator()(const itch::AddOrder& m) {
        if (!on) return;
        const bool buy = m.side == 'B';
        const std::uint32_t best = buy ? bbo.bid_px : bbo.ask_px;
        int k;
        if (m.price == best) k = queue_bucket(static_cast<double>(buy ? bbo.bid_qty : bbo.ask_qty), depth);
        else return;  // behind the best, or crossing
        q.adds[static_cast<std::size_t>(k)] += 1;
        live[m.ref] = {k, buy, m.price, ts};
    }
    void fill(std::uint64_t ref) {
        const auto it = live.find(ref);
        if (it == live.end()) return;
        const Live& l = it->second;
        q.fills[static_cast<std::size_t>(l.bucket)] += 1;
        q.wait_s[static_cast<std::size_t>(l.bucket)] += static_cast<double>(ts - l.ts) / kSec;
        marks.push_back({ts, l.bucket, l.buy, l.px / static_cast<double>(tick)});
        live.erase(it);
    }
    void operator()(const itch::OrderExecuted& m) { fill(m.ref); }
    void operator()(const itch::OrderExecutedPrice& m) { fill(m.ref); }
    void operator()(const itch::OrderDelete& m) { live.erase(m.ref); }
    void operator()(const itch::OrderReplace& m) { live.erase(m.orig_ref); }
    template <class T>
    void operator()(const T&) {}
};

// One pass over a symbol-day. With `g` (a fitted micro-price adjustment per state) it scores
// predictions; without, it counts transitions.
DayResult run_day(const fs::path& dir, std::uint16_t loc, const std::array<double, kStates>* g) {
    thread_local std::unique_ptr<book::TickBook> slot;
    if (slot) slot->clear(); else slot = std::make_unique<book::TickBook>();
    book::TickBook& b = *slot;
    book::ItchApply<book::TickBook> ap{b};
    State st;
    DayResult out;
    const std::uint32_t tick = 100;  // $0.01 in ITCH price units; studied stocks trade above $1

    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    book::Bbo prev{};
    bool have_prev = false;
    std::int64_t cur_iv = -1;
    double iv_ofi = 0, iv_depth_sum = 0, iv_mid_start = 0, last_mid = 0, last_depth = 0;
    std::uint64_t iv_events = 0;
    int last_sample = -1;  // state of the previous micro-price sample
    double last_sample_mid = 0;

    struct Pending {
        std::uint64_t ts;
        double mid;
        std::array<double, 3> pred;
        bool a_done, b_done;
    };
    std::deque<Pending> pend;
    std::size_t a_next = 0;  // first pending item without a next-mid-change target

    std::unordered_map<std::uint64_t, Live> live;  // tracked orders not yet filled
    std::deque<Markout> marks;
    double depth_ewma = 0;
    book::Bbo before{};
    QueueTrack qt{live, marks, out.q, before, depth_ewma};

    auto close_interval = [&](std::int64_t k) {
        Interval iv;
        iv.ofi = iv_ofi;
        iv.dmid = last_mid - iv_mid_start;
        iv.depth = iv_events ? iv_depth_sum / static_cast<double>(iv_events) : last_depth;
        iv.window = static_cast<std::uint32_t>(static_cast<std::uint64_t>(k) * kInterval / kWindow);
        out.iv.push_back(iv);
        iv_ofi = iv_depth_sum = 0;
        iv_events = 0;
        iv_mid_start = last_mid;
    };

    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        if (!g) {
            // Queue tracking sees the book as the message found it.
            before = b.bbo();
            qt.ts = ts;
            qt.on = st.trading == 'T' && ts >= kStart && ts < kEnd && before.bid_px && before.ask_px == before.bid_px + tick;
            if (qt.on || !live.empty()) itch::dispatch(rec.data, rec.len, qt);
        }
        itch::dispatch(rec.data, rec.len, st);
        ap.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, ap);
        const book::Bbo cur = b.bbo();
        const bool valid = st.trading == 'T' && cur.bid_px && cur.ask_px && cur.ask_px > cur.bid_px;
        if (!valid || ts < kStart) {
            have_prev = valid && ts >= kStart - kInterval;
            if (valid) prev = cur, last_mid = (cur.bid_px + cur.ask_px) / 2.0 / tick, last_depth = (cur.bid_qty + cur.ask_qty) / 2.0;
            continue;
        }
        if (ts >= kEnd) break;
        const double mid = (cur.bid_px + cur.ask_px) / 2.0 / tick;
        depth_ewma = depth_ewma ? 0.999 * depth_ewma + 0.001 * (cur.bid_qty + cur.ask_qty) / 2.0 : (cur.bid_qty + cur.ask_qty) / 2.0;
        // Markout of a fill: the mid in force kHorizon later, against the fill price, per share.
        while (!marks.empty() && marks.front().ts + kHorizon <= ts) {
            const Markout& m = marks.front();
            out.q.markout[static_cast<std::size_t>(m.bucket)] += m.buy ? last_mid - m.px : m.px - last_mid;
            out.q.marked[static_cast<std::size_t>(m.bucket)] += 1;
            marks.pop_front();
        }

        // Time target of pending predictions: the mid in force kHorizon after each.
        while (!pend.empty() && pend.front().ts + kHorizon <= ts) {
            Pending& p = pend.front();
            if (!p.b_done) {
                for (int k = 0; k < 3; ++k) out.e.se[1][k] += (last_mid - p.pred[k]) * (last_mid - p.pred[k]);
                ++out.e.n[1];
                p.b_done = true;
            }
            if (!p.a_done) break;
            pend.pop_front();
            if (a_next) --a_next;
        }
        if (mid != last_mid && g) {
            for (std::size_t i = a_next; i < pend.size(); ++i) {
                for (int k = 0; k < 3; ++k) out.e.se[0][k] += (mid - pend[i].pred[k]) * (mid - pend[i].pred[k]);
                ++out.e.n[0];
                pend[i].a_done = true;
            }
            a_next = pend.size();
            while (!pend.empty() && pend.front().a_done && pend.front().b_done) pend.pop_front(), --a_next;
        }

        const std::int64_t k = static_cast<std::int64_t>((ts - kStart) / kInterval);
        if (cur_iv < 0) cur_iv = k, iv_mid_start = last_mid ? last_mid : mid;
        while (cur_iv < k) close_interval(cur_iv++);

        if (have_prev) iv_ofi += static_cast<double>(ofi_event(prev, cur));
        iv_depth_sum += (cur.bid_qty + cur.ask_qty) / 2.0;
        ++iv_events;

        const bool changed = !have_prev || !(cur == prev);
        const std::uint32_t spread = (cur.ask_px - cur.bid_px) / tick;
        out.spread_obs[std::min<std::uint32_t>(spread, 3) - 1] += changed;
        if (changed && spread >= 1 && spread <= 2 && (cur.ask_px - cur.bid_px) % tick == 0) {
            const double imb = static_cast<double>(cur.bid_qty) / static_cast<double>(cur.bid_qty + cur.ask_qty);
            const int x = state_of(static_cast<int>(spread), imb);
            if (!g) {
                if (last_sample >= 0) out.c.add(last_sample, x, mid - last_sample_mid);
            } else {
                const double bid = cur.bid_px / static_cast<double>(tick), ask = cur.ask_px / static_cast<double>(tick);
                pend.push_back({ts, mid, {mid, imb * ask + (1 - imb) * bid, mid + (*g)[static_cast<std::size_t>(x)]}, false, false});
            }
            last_sample = x;
            last_sample_mid = mid;
        }
        prev = cur;
        have_prev = true;
        last_mid = mid;
        last_depth = (cur.bid_qty + cur.ask_qty) / 2.0;
    }
    if (cur_iv >= 0) close_interval(cur_iv);
    if (b.order_count() > 65536) slot.reset();
    return out;
}

std::vector<fs::path> split_paths(const std::string& v) {
    std::vector<fs::path> out;
    std::size_t i = 0;
    while (i <= v.size()) {
        const std::size_t j = std::min(v.find(',', i), v.size());
        if (j > i) out.emplace_back(v.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

std::string day_of(const fs::path& p) {
    const std::string s = p.filename().string();  // store-S120825-v50
    const auto at = s.find('S');
    return at == std::string::npos || s.size() < at + 7 ? s : "20" + s.substr(at + 5, 2) + "-" + s.substr(at + 1, 2) + "-" + s.substr(at + 3, 2);
}

double median(std::vector<double> v) {
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> train, test;
    std::size_t n_symbols = 30;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", a.c_str());
            return 2;
        }
        if (a == "--train") train = split_paths(argv[++i]);
        else if (a == "--test") test = split_paths(argv[++i]);
        else if (a == "--symbols") n_symbols = std::strtoul(argv[++i], nullptr, 10);
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (train.empty() || test.empty() || n_symbols == 0) {
        std::fprintf(stderr, "usage: itch_study --train <store>[,..] --test <store>[,..] [--symbols N]\n");
        return 2;
    }
    std::vector<fs::path> days = train;
    days.insert(days.end(), test.begin(), test.end());
    for (const auto& d : days)
        if (!fs::is_directory(d)) {
            std::fprintf(stderr, "not a store: %s\n", d.c_str());
            return 2;
        }

    // Universe: the most active symbols of the first train day that trade on every day.
    std::vector<std::map<std::string, std::uint16_t>> by_name(days.size());
    for (std::size_t d = 0; d < days.size(); ++d) {
        const auto locs = locates(days[d]);
        std::vector<std::string> names(locs.size());
        parallel(locs.size(), [&](std::size_t i) { names[i] = read_symbol(days[d], locs[i]); });
        for (std::size_t i = 0; i < locs.size(); ++i)
            if (!names[i].empty()) by_name[d][names[i]] = locs[i];
    }
    std::vector<std::pair<std::uint64_t, std::string>> active;
    for (const auto& [name, loc] : by_name[0]) {
        bool everywhere = true;
        for (std::size_t d = 1; d < days.size(); ++d) everywhere &= by_name[d].contains(name);
        if (!everywhere) continue;
        data::SymbolReader rd(days[0], loc);
        std::uint64_t n = 0;
        for (const auto& e : rd.index()) n += e.n_msgs;
        active.emplace_back(n, name);
    }
    std::sort(active.rbegin(), active.rend());
    if (active.size() > n_symbols) active.resize(n_symbols);
    const std::size_t S = active.size(), D = days.size(), T = train.size();

    // Pass 1: every symbol-day, counting micro-price transitions.
    std::vector<DayResult> res(S * D);
    parallel(S * D, [&](std::size_t i) {
        const std::size_t s = i / D, d = i % D;
        res[i] = run_day(days[d], by_name[d].at(active[s].second), nullptr);
    });
    // Micro-price adjustment per symbol from the train days only.
    std::vector<std::array<double, kStates>> g(S);
    for (std::size_t s = 0; s < S; ++s) {
        Counts c;
        for (std::size_t d = 0; d < T; ++d) c.merge(res[s * D + d].c);
        g[s] = adjustment(c);
    }
    // Pass 2: score the predictors on every day (train days in sample, test days out of sample).
    std::vector<Errors> err(S * D);
    parallel(S * D, [&](std::size_t i) {
        const std::size_t s = i / D, d = i % D;
        err[i] = run_day(days[d], by_name[d].at(active[s].second), &g[s]).e;
    });

    // OFI: per-symbol slope on train intervals; universal beta = c / depth^lambda from
    // half-hour windows of every train symbol-day.
    std::vector<double> lx, ly;
    for (std::size_t s = 0; s < S; ++s)
        for (std::size_t d = 0; d < T; ++d) {
            std::map<std::uint32_t, std::pair<std::vector<double>, std::vector<double>>> w;
            std::map<std::uint32_t, double> depth;
            for (const Interval& iv : res[s * D + d].iv) {
                w[iv.window].first.push_back(iv.ofi);
                w[iv.window].second.push_back(iv.dmid);
                depth[iv.window] += iv.depth;
            }
            for (auto& [k, xy] : w) {
                const Fit f = ols(xy.first, xy.second);
                const double dep = depth[k] / static_cast<double>(xy.first.size());
                if (f.b > 0 && dep > 0) lx.push_back(std::log(dep)), ly.push_back(std::log(f.b));
            }
        }
    const Fit depth_law = ols(lx, ly);  // log beta = log c - lambda log depth
    const double lambda = -depth_law.b, logc = depth_law.a;

    std::printf("# Microstructure study\n\n");
    std::printf("Generated by `itch_study` (`scripts/study.sh`). Train: ");
    for (std::size_t d = 0; d < T; ++d) std::printf("%s%s", d ? ", " : "", day_of(days[d]).c_str());
    std::printf("; test: ");
    for (std::size_t d = T; d < D; ++d) std::printf("%s%s", d > T ? ", " : "", day_of(days[d]).c_str());
    std::printf(". Universe: the %zu most active symbols of %s that trade on every day. "
                "Window 10:00 to 15:30 ET, trading state only. Every parameter comes from the train days; "
                "test days are scored with them unchanged.\n\n", S, day_of(days[0]).c_str());

    std::printf("## Order flow imbalance (Cont, Kukanov and Stoikov, 2014)\n\n");
    std::printf("OFI sums the order flow at the best quotes over 10 s intervals; the model is "
                "dmid = beta OFI. Per-symbol beta and in-sample R2 come from the train days; out-of-sample R2 uses "
                "that beta on the test days. The universal model replaces each symbol's beta by c / depth^lambda, "
                "two numbers fitted on half-hour windows of all train symbol-days (%zu windows): lambda = %.2f, "
                "R2 of the log fit %.2f. The paper reports lambda close to 1.\n\n", lx.size(), lambda, depth_law.r2);
    std::printf("| Symbol | Mean depth (sh) | beta (ticks per 1,000 sh) | R2 train | R2 test, own beta | R2 test, universal |\n");
    std::printf("|---|---|---|---|---|---|\n");
    std::vector<double> r2_own, r2_uni, r2_in;
    for (std::size_t s = 0; s < S; ++s) {
        std::vector<double> x, y, dep;
        for (std::size_t d = 0; d < T; ++d)
            for (const Interval& iv : res[s * D + d].iv) x.push_back(iv.ofi), y.push_back(iv.dmid), dep.push_back(iv.depth);
        const Fit f = ols(x, y);
        std::vector<double> yt, p_own, p_uni;
        double dsum = 0;
        for (double v : dep) dsum += v;
        for (std::size_t d = T; d < D; ++d) {
            std::map<std::uint32_t, std::pair<double, std::size_t>> wd;
            for (const Interval& iv : res[s * D + d].iv) wd[iv.window].first += iv.depth, ++wd[iv.window].second;
            for (const Interval& iv : res[s * D + d].iv) {
                const auto& [sum, n] = wd[iv.window];
                const double depth_w = sum / static_cast<double>(n);
                yt.push_back(iv.dmid);
                p_own.push_back(f.a + f.b * iv.ofi);
                p_uni.push_back(depth_w > 0 ? std::exp(logc) * std::pow(depth_w, -lambda) * iv.ofi : 0);
            }
        }
        const double ro = r2_of(p_own, yt), ru = r2_of(p_uni, yt);
        r2_own.push_back(ro), r2_uni.push_back(ru), r2_in.push_back(f.r2);
        std::printf("| %s | %.0f | %.3f | %.2f | %.2f | %.2f |\n", active[s].second.c_str(),
                    dep.empty() ? 0 : dsum / static_cast<double>(dep.size()), f.b * 1000, f.r2, ro, ru);
    }
    std::printf("\nMedian over symbols: R2 train %.2f, test with own beta %.2f, test with the universal model %.2f.\n\n",
                median(r2_in), median(r2_own), median(r2_uni));

    std::printf("## Micro-price (Stoikov, 2018)\n\n");
    std::printf("States: top-of-book imbalance bid / (bid + ask) in %d buckets, times spread of 1 or 2 ticks, "
                "sampled at every change of the best quotes. The adjustment G(state) = sum of g_k (g_1 = "
                "(1 - Q)^-1 R, g_k+1 = (1 - Q)^-1 T g_k, eight terms) is estimated per symbol from the symmetrised "
                "train transitions. Each predictor is scored by its mean squared error against the mid at the next mid "
                "change and the mid 1 s later; the table gives the error reduction against the mid itself (higher is "
                "better, 0 means no better than the mid). Only symbols quoted at 1-2 ticks for at least half of their "
                "quote changes are shown (large-tick stocks, where the model applies).\n\n", kBuckets);
    std::printf("| Symbol | 1-2 tick share | Samples test | Next change: weighted mid | Next change: micro-price | 1 s: weighted mid | 1 s: micro-price | Micro-price in sample, next change |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    std::vector<double> m_next, w_next, m_1s, w_1s;
    std::array<double, kBuckets> g_avg{};
    int g_n = 0;
    for (std::size_t s = 0; s < S; ++s) {
        std::uint64_t narrow = 0, all = 0;
        for (std::size_t d = 0; d < T; ++d) {
            const auto& o = res[s * D + d].spread_obs;
            narrow += o[0] + o[1], all += o[0] + o[1] + o[2];
        }
        const double share = all ? static_cast<double>(narrow) / static_cast<double>(all) : 0;
        if (share < 0.5) continue;
        Errors in, out;
        for (std::size_t d = 0; d < T; ++d) in.add(err[s * D + d]);
        for (std::size_t d = T; d < D; ++d) out.add(err[s * D + d]);
        if (!out.n[0] || !out.n[1] || !in.n[0]) continue;
        auto red = [](const Errors& e, int k, int p) { return 1 - e.se[k][p] / e.se[k][0]; };
        w_next.push_back(red(out, 0, 1)), m_next.push_back(red(out, 0, 2));
        w_1s.push_back(red(out, 1, 1)), m_1s.push_back(red(out, 1, 2));
        for (int b = 0; b < kBuckets; ++b) g_avg[static_cast<std::size_t>(b)] += g[s][static_cast<std::size_t>(b)];
        ++g_n;
        std::printf("| %s | %.2f | %llu | %+.1f%% | %+.1f%% | %+.1f%% | %+.1f%% | %+.1f%% |\n", active[s].second.c_str(), share,
                    static_cast<unsigned long long>(out.n[0]), 100 * red(out, 0, 1), 100 * red(out, 0, 2),
                    100 * red(out, 1, 1), 100 * red(out, 1, 2), 100 * red(in, 0, 2));
    }
    std::printf("\nMedian over the %zu symbols, test days: next change, weighted mid %+.1f%%, micro-price %+.1f%%; "
                "1 s, weighted mid %+.1f%%, micro-price %+.1f%%.\n\n", m_next.size(), 100 * median(w_next),
                100 * median(m_next), 100 * median(w_1s), 100 * median(m_1s));
    if (g_n) {
        std::printf("Mean adjustment G at a 1-tick spread by imbalance bucket, in ticks (train fit, averaged over those symbols):\n\n");
        std::printf("| Imbalance |");
        for (int b = 0; b < kBuckets; ++b) std::printf(" %.1f-%.1f |", b / 10.0, (b + 1) / 10.0);
        std::printf("\n|---|");
        for (int b = 0; b < kBuckets; ++b) std::printf("---|");
        std::printf("\n| G (ticks) |");
        for (int b = 0; b < kBuckets; ++b) std::printf(" %+.3f |", g_avg[static_cast<std::size_t>(b)] / g_n);
        std::printf("\n");
    }
    // Queue position: descriptive, over the large-tick symbols and every day.
    Queue q;
    std::size_t q_syms = 0;
    for (std::size_t s = 0; s < S; ++s) {
        std::uint64_t narrow = 0, all = 0;
        for (std::size_t d = 0; d < T; ++d) {
            const auto& o = res[s * D + d].spread_obs;
            narrow += o[0] + o[1], all += o[0] + o[1] + o[2];
        }
        if (!all || static_cast<double>(narrow) / static_cast<double>(all) < 0.5) continue;
        ++q_syms;
        for (std::size_t d = 0; d < D; ++d) q.add(res[s * D + d].q);
    }
    std::printf("\n## Queue position (Moallemi and Yuan, 2017)\n\n");
    std::printf("Every limit order that joins the best bid or ask while the spread is one tick, on the same %zu "
                "large-tick symbols over all %zu days, by the shares already queued ahead of it relative to the "
                "symbol's typical best-level size (an exponential average of (bid + ask size) / 2). Filled: at "
                "least one execution before the order is cancelled, deleted or replaced. Markout: the mid 1 s after "
                "the first fill against the fill price, per share, in ticks (+0.5 is the half spread kept with no "
                "move; below 0 the fill lost money). Value per order: fill rate times markout.\n\n", q_syms, D);
    std::printf("| Queue ahead / typical level | Orders | Filled | Mean wait to fill (s) | Markout 1 s (ticks) | Value per order (ticks) |\n");
    std::printf("|---|---|---|---|---|---|\n");
    const char* label[Queue::kPos] = {"0 (empty level)", "0 - 0.25", "0.25 - 0.5", "0.5 - 1", "1 - 2", "2 or more"};
    for (std::size_t k = 0; k < Queue::kPos; ++k) {
        if (q.adds[k] == 0) continue;
        const double fr = q.fills[k] / q.adds[k], mo = q.marked[k] ? q.markout[k] / q.marked[k] : NAN;
        std::printf("| %s | %.0f | %.1f%% | %.2f | %+.3f | %+.4f |\n", label[k], q.adds[k], 100 * fr,
                    q.fills[k] ? q.wait_s[k] / q.fills[k] : NAN, mo, fr * mo);
    }
    std::printf("\n## Caveats\n\n"
                "- ITCH is Nasdaq's book only; the consolidated quote is tighter, so depth and imbalance are partial.\n"
                "- The most active names are mostly ETFs and leveraged ETFs; two test days measure stability across days, "
                "not across regimes.\n"
                "- OFI explains the contemporaneous price change (as in the paper); it is not a forecast. The micro-price "
                "is one: it uses only the book at the time of the prediction.\n\n"
                "## References\n\n"
                "- R. Cont, A. Kukanov, S. Stoikov. The price impact of order book events. Journal of Financial "
                "Econometrics 12(1), 47-88, 2014. arXiv:1011.6402.\n"
                "- S. Stoikov. The micro-price: a high-frequency estimator of future prices. Quantitative Finance "
                "18(12), 1959-1966, 2018.\n"
                "- C. Moallemi, K. Yuan. A model for queue position valuation in a limit order book. Working paper, "
                "Columbia University, 2017.\n");
    return 0;
}
