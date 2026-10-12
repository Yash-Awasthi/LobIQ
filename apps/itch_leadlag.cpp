// Lead-lag between related symbols on stored Nasdaq days: after a mid move in A, how soon does B's
// mid move, and how often in the same direction (sign-adjusted for inverse products)? Prints Markdown.
// Usage: itch_leadlag <store>[,<store>..] [A:B[:-1],..]   Env: THREADS.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"
#include "strategy/microstructure.hpp"

namespace {

using namespace hft;
using namespace strategy::micro;
namespace fs = std::filesystem;

constexpr std::uint64_t kSec = 1'000'000'000;
constexpr std::uint64_t kStart = 10 * 3600 * kSec, kEnd = (15 * 3600 + 30 * 60) * kSec;
constexpr const char* kBinName[] = {"<1 us", "1-10 us", "10-100 us", "0.1-1 ms", "1-10 ms", "10 ms-1 s"};

struct State {
    char trading = 'T';  // real feeds send H before the open; synthetic days send none
    void operator()(const itch::TradingAction& m) { trading = m.state; }
    template <class T>
    void operator()(const T&) {}
};

// Mid changes of one symbol-day inside the window, trading state only.
std::vector<Move> mid_moves(const fs::path& dir, std::uint16_t loc) {
    thread_local std::unique_ptr<book::TickBook> slot;
    if (slot) slot->clear(); else slot = std::make_unique<book::TickBook>();
    book::TickBook& b = *slot;
    book::ItchApply<book::TickBook> ap{b};
    State st;
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    std::vector<Move> out;
    std::uint64_t last_mid2 = 0;
    while (rd.next(rec)) {
        itch::dispatch(rec.data, rec.len, st);
        itch::dispatch(rec.data, rec.len, ap);
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        if (ts >= kEnd) break;
        const book::Bbo q = b.bbo();
        if (st.trading != 'T' || !q.bid_px || !q.ask_px || q.ask_px <= q.bid_px) continue;
        const std::uint64_t mid2 = std::uint64_t{q.bid_px} + q.ask_px;
        if (ts >= kStart && last_mid2 && mid2 != last_mid2) out.push_back({ts, mid2 > last_mid2 ? 1 : -1});
        last_mid2 = mid2;
    }
    if (b.order_count() > 65536) slot.reset();
    return out;
}

std::map<std::string, std::uint16_t> symbols(const fs::path& store) {
    std::map<std::string, std::uint16_t> m;
    std::ifstream tsv(store / "symbols.tsv");
    std::string loc, name;
    while (tsv >> loc >> name) m[name] = static_cast<std::uint16_t>(std::stoul(loc));
    return m;
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

std::vector<std::string> split(const std::string& v, char c) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= v.size()) {
        const std::size_t j = std::min(v.find(c, i), v.size());
        if (j > i) out.push_back(v.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

double pct(std::uint64_t a, std::uint64_t b) { return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 || argv[1][0] == '-') {
        std::fprintf(stderr, "usage: itch_leadlag <store>[,<store>..] [A:B[:-1],..]\n");
        return 2;
    }
    const std::vector<std::string> stores = split(argv[1], ',');
    for (const auto& s : stores)
        if (!fs::exists(fs::path(s) / "symbols.tsv")) {
            std::fprintf(stderr, "not a store with symbols.tsv: %s\n", s.c_str());
            return 2;
        }
    const std::string pairs_arg = argc > 2 ? argv[2]
        : "SPY:IVV,SPY:VOO,SPY:QQQ,QQQ:QQQM,QQQ:TQQQ,QQQ:SQQQ:-1,TQQQ:SQQQ:-1,IWM:TNA,SPY:UPRO,"
          "NVDA:NVDL,TSLA:TSLL,SMH:SOXL,SOXX:SOXL,GOOGL:GOOG,IBIT:FBTC";
    struct Pair {
        std::string a, b;
        int sign;
    };
    std::vector<Pair> pairs;
    for (const auto& p : split(pairs_arg, ',')) {
        const auto f = split(p, ':');
        if (f.size() < 2 || f.size() > 3 || (f.size() == 3 && f[2] != "-1")) {
            std::fprintf(stderr, "bad pair %s (want A:B or A:B:-1)\n", p.c_str());
            return 2;
        }
        pairs.push_back({f[0], f[1], f.size() == 3 ? -1 : 1});
    }

    // Every needed symbol-day once.
    std::vector<std::map<std::string, std::uint16_t>> names;
    for (const auto& s : stores) names.push_back(symbols(s));
    std::vector<std::pair<std::size_t, std::string>> jobs;
    for (std::size_t d = 0; d < stores.size(); ++d)
        for (const auto& p : pairs)
            for (const auto& s : {p.a, p.b})
                if (names[d].contains(s) && std::find(jobs.begin(), jobs.end(), std::pair{d, s}) == jobs.end())
                    jobs.emplace_back(d, s);
    std::vector<std::vector<Move>> moves(jobs.size());
    parallel(jobs.size(), [&](std::size_t i) { moves[i] = mid_moves(stores[jobs[i].first], names[jobs[i].first].at(jobs[i].second)); });
    auto find = [&](std::size_t d, const std::string& s) -> const std::vector<Move>* {
        for (std::size_t i = 0; i < jobs.size(); ++i)
            if (jobs[i].first == d && jobs[i].second == s) return &moves[i];
        return nullptr;
    };

    std::printf("# Lead-lag\n\nGenerated by `itch_leadlag` (`scripts/leadlag.sh`) over %zu Nasdaq days, 10:00 to 15:30 ET, "
                "trading state only. For every mid move of A, the first mid move of B after it (within 1 s): how often it "
                "goes the same way (opposite for inverse products, marked -1) and how long it took. Read both directions: "
                "the leader is the side whose moves the other follows more often. 50%% means no information.\n\n",
                stores.size());
    std::printf("Placebo: the same with every move of A shifted 2 s later, which keeps slow common drift but removes "
                "fast information; the edge is same direction minus placebo, in percentage points.\n\n");
    std::printf("| A -> B | Days | Moves of A | B answers within 1 s | Same direction | Placebo | Edge (pp) | Median delay (us) | Same-direction answers slower than 10 us |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|\n");
    std::vector<std::pair<std::string, LeadResponse>> all;
    for (const auto& p : pairs)
        for (int dir = 0; dir < 2; ++dir) {
            const std::string& a = dir ? p.b : p.a;
            const std::string& b = dir ? p.a : p.b;
            LeadResponse r, plc;
            int days = 0;
            for (std::size_t d = 0; d < stores.size(); ++d) {
                const auto* ma = find(d, a);
                const auto* mb = find(d, b);
                if (!ma || !mb) continue;
                ++days;
                r.add(lead_response(*ma, *mb, p.sign));
                plc.add(lead_response(*ma, *mb, p.sign, 2 * kSec));
            }
            if (!days) continue;
            std::sort(r.delays.begin(), r.delays.end());
            const double med = r.delays.empty() ? 0 : static_cast<double>(r.delays[r.delays.size() / 2]) / 1e3;
            std::uint64_t slow = 0;
            for (std::size_t k = 2; k < kLeadEdge.size(); ++k) slow += r.bins[k];
            const std::string label = a + " -> " + b + (p.sign < 0 ? " (-1)" : "");
            const double same = pct(r.same, r.answered), base = pct(plc.same, plc.answered);
            std::printf("| %s | %d | %llu | %.1f%% | %.1f%% | %.1f%% | %+.1f | %.0f | %.1f%% |\n", label.c_str(), days,
                        static_cast<unsigned long long>(r.leads), pct(r.answered, r.leads), same, base, same - base, med,
                        pct(slow, r.same));
            all.emplace_back(label, std::move(r));
        }
    std::printf("\n## Delay of same-direction answers\n\n| A -> B |");
    for (const char* n : kBinName) std::printf(" %s |", n);
    std::printf("\n|---|");
    for (std::size_t i = 0; i < kLeadEdge.size(); ++i) std::printf("---|");
    std::printf("\n");
    for (const auto& [label, r] : all) {
        std::printf("| %s |", label.c_str());
        for (std::uint64_t n : r.bins) std::printf(" %.1f%% |", pct(n, r.same));
        std::printf("\n");
    }
    std::printf("\nCaveats: Nasdaq's book only; the same products quote on other venues, so a move seen here can follow "
                "one elsewhere. Same-timestamp answers are the matching engine processing both books in one event "
                "(none here: an answer must come strictly after the lead).\n");
    return 0;
}
