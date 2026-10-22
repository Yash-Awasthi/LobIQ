// Markov chain of one symbol's top of book over a stored day (src/strategy/markov.hpp): states,
// moves between them, and how well each state's direction counts from the morning predict the
// afternoon's mid moves. Markdown by default, JSON with --json (hft studio draws it).
// Usage: itch_markov <store> <symbol> [--buckets N] [--split HH:MM] [--json]

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"
#include "strategy/markov.hpp"

namespace {

using namespace hft;

constexpr std::uint64_t kNsPerMin = 60'000'000'000ull;
constexpr std::uint64_t kOpen = (9 * 60 + 35) * kNsPerMin, kClose = (15 * 60 + 55) * kNsPerMin;  // auctions left out

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage = "usage: itch_markov <store> <symbol> [--buckets N] [--split HH:MM] [--json]";
    int buckets = 5;
    std::uint64_t split = (12 * 60 + 45) * kNsPerMin;
    bool json = false;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) json = true;
        else if (std::strcmp(argv[i], "--buckets") == 0 && i + 1 < argc) buckets = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--split") == 0 && i + 1 < argc) {
            unsigned h, m;
            if (std::sscanf(argv[++i], "%2u:%2u", &h, &m) != 2 || h > 23 || m > 59) return std::fprintf(stderr, "%s\n", usage), 2;
            split = (h * 60 + m) * kNsPerMin;
        } else if (argv[i][0] == '-') return std::fprintf(stderr, "%s\n", usage), 2;
        else pos.push_back(argv[i]);
    }
    if (pos.size() != 2 || buckets < 2 || buckets > 20 || split <= kOpen || split >= kClose)
        return std::fprintf(stderr, "%s\n", usage), 2;
    const std::filesystem::path dir = pos[0];
    const auto symbols = data::read_symbols(dir);
    const auto it = symbols.find(pos[1]);
    if (it == symbols.end()) return std::fprintf(stderr, "symbol %s not in %s\n", pos[1], dir.c_str()), 2;

    constexpr int kMaxSpread = 3;
    strategy::MarkovBook mk(buckets, kMaxSpread, 100);  // penny tick in ITCH units
    book::TickBook b;
    book::ItchApply<book::TickBook> ap{b};
    data::SymbolReader rd(dir, it->second);
    data::Record rec{};
    std::uint64_t events = 0;
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        ap.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, ap);
        if (ts < kOpen) continue;
        if (ts >= kClose) break;
        mk.observe(b.bbo(), ts, ts < split);
        ++events;
    }

    const int n = mk.states();
    std::uint64_t total_ns = 0;
    for (int s = 0; s < n; ++s) total_ns += mk.time_ns(s);
    auto p_up = [&](int s) {
        const double u = static_cast<double>(mk.ups(s)), d = static_cast<double>(mk.downs(s));
        return u + d > 0 ? u / (u + d) : -1.0;  // -1: no move seen from this state
    };
    const double hit = mk.test_moves() ? static_cast<double>(mk.test_hits()) / static_cast<double>(mk.test_moves()) : 0;
    char split_s[8];
    std::snprintf(split_s, sizeof split_s, "%02" PRIu64 ":%02" PRIu64, split / kNsPerMin / 60, split / kNsPerMin % 60);

    if (json) {
        std::printf("{\"symbol\":\"%s\",\"buckets\":%d,\"max_spread\":%d,\"split\":\"%s\",\"events\":%" PRIu64 ",\"states\":[",
                    pos[1], buckets, kMaxSpread, split_s, events);
        for (int s = 0; s < n; ++s)
            std::printf("%s{\"id\":%d,\"spread\":%d,\"imb_lo\":%.4f,\"imb_hi\":%.4f,\"time_share\":%.6f,\"up\":%" PRIu64
                        ",\"down\":%" PRIu64 ",\"p_up\":%.6f}",
                        s ? "," : "", s, s / buckets + 1, static_cast<double>(s % buckets) / buckets,
                        static_cast<double>(s % buckets + 1) / buckets,
                        total_ns ? static_cast<double>(mk.time_ns(s)) / static_cast<double>(total_ns) : 0.0, mk.ups(s), mk.downs(s), p_up(s));
        std::printf("],\"transitions\":[");
        bool first = true;
        for (int a = 0; a < n; ++a)
            for (int c = 0; c < n; ++c)
                if (const std::uint64_t k = mk.transitions(a, c)) std::printf("%s[%d,%d,%" PRIu64 "]", first ? "" : ",", a, c, k), first = false;
        std::printf("],\"test\":{\"moves\":%" PRIu64 ",\"hits\":%" PRIu64 ",\"hit_rate\":%.6f,\"baseline\":%.6f}}\n", mk.test_moves(),
                    mk.test_hits(), hit, mk.baseline());
        return 0;
    }

    std::printf("# Markov chain of the book: %s, %s\n\n", pos[1], dir.filename().c_str());
    std::printf("State: spread in ticks (%d means %d or more) and the bid's share of the best-level size in %d buckets. "
                "Direction counts from 09:35 to %s; scored on the mid moves from %s to 15:55.\n\n",
                kMaxSpread, kMaxSpread, buckets, split_s, split_s);
    std::printf("## Prediction, out of sample\n\n| Mid moves | Predicted right | Hit rate | Always the more frequent direction |\n|---|---|---|---|\n");
    std::printf("| %" PRIu64 " | %" PRIu64 " | %.1f%% | %.1f%% |\n\n", mk.test_moves(), mk.test_hits(), 100 * hit, 100 * mk.baseline());
    std::printf("## States\n\n| Spread | Bid share | Time | Moves up | Moves down | P(next move up) |\n|---|---|---|---|---|---|\n");
    for (int s = 0; s < n; ++s) {
        if (!mk.time_ns(s) && !mk.ups(s) && !mk.downs(s)) continue;
        std::printf("| %d | %.2f-%.2f | %.1f%% | %" PRIu64 " | %" PRIu64 " | %s |\n", s / buckets + 1, static_cast<double>(s % buckets) / buckets,
                    static_cast<double>(s % buckets + 1) / buckets,
                    total_ns ? 100.0 * static_cast<double>(mk.time_ns(s)) / static_cast<double>(total_ns) : 0.0, mk.ups(s), mk.downs(s),
                    p_up(s) < 0 ? "-" : std::to_string(p_up(s)).substr(0, 5).c_str());
    }
    return 0;
}
