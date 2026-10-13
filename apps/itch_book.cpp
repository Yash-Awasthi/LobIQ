// The order book of one symbol at any moment of a stored day: starts from the last book checkpoint
// before that time and replays only the messages after it.
// Usage: itch_book <store> <symbol> <HH:MM:SS[.fraction]> [levels] [--no-checkpoint]

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "data/checkpoint.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"

namespace {

using namespace hft;

// "HH:MM:SS" or "HH:MM:SS.fraction" -> nanoseconds since midnight; -1 when malformed.
std::int64_t parse_time(const char* s) {
    unsigned h, m, sec;
    int used = 0;
    if (std::sscanf(s, "%2u:%2u:%2u%n", &h, &m, &sec, &used) != 3 || h > 23 || m > 59 || sec > 59) return -1;
    std::int64_t ns = (static_cast<std::int64_t>(h) * 3600 + m * 60 + sec) * 1'000'000'000;
    if (s[used] == '\0') return ns;
    if (s[used] != '.') return -1;
    std::int64_t frac = 0, scale = 100'000'000;
    for (const char* p = s + used + 1; *p; ++p, scale /= 10) {
        if (*p < '0' || *p > '9' || scale == 0) return -1;
        frac += (*p - '0') * scale;
    }
    return ns + frac;
}

void print_side(const book::TickBook& b, book::Side side, int levels) {
    auto l = b.best_level(side);
    for (int i = 0; i < levels && l; ++i, l = b.next_level_info(side, l->price))
        std::printf("  %s %10.4f  %10" PRIu64 "\n", side == book::Side::Buy ? "bid" : "ask", l->price / 1e4, l->qty);
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage = "usage: itch_book <store> <symbol> <HH:MM:SS[.fraction]> [levels] [--no-checkpoint]";
    bool use_checkpoints = true;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-checkpoint") == 0) use_checkpoints = false;
        else if (argv[i][0] == '-') return std::fprintf(stderr, "%s\n", usage), 2;
        else pos.push_back(argv[i]);
    }
    if (pos.size() < 3 || pos.size() > 4) return std::fprintf(stderr, "%s\n", usage), 2;
    const std::filesystem::path dir = pos[0];
    const auto symbols = data::read_symbols(dir);
    const auto it = symbols.find(pos[1]);
    if (it == symbols.end()) return std::fprintf(stderr, "symbol %s not in %s\n", pos[1], dir.c_str()), 2;
    const std::int64_t target = parse_time(pos[2]);
    if (target < 0) return std::fprintf(stderr, "bad time %s (want HH:MM:SS[.fraction])\n", pos[2]), 2;
    const int levels = pos.size() == 4 ? std::atoi(pos[3]) : 10;
    if (levels <= 0) return std::fprintf(stderr, "%s\n", usage), 2;
    const std::uint16_t loc = it->second;

    data::SymbolReader rd(dir, loc);
    const auto& idx = rd.index();
    book::TickBook b;
    std::size_t start = 0;  // chunk the replay starts at
    if (use_checkpoints) {
        const auto es = data::read_checkpoints(dir, loc);
        std::ifstream ckp(data::checkpoint_path(dir, loc, ".ckp"), std::ios::binary);
        for (std::size_t i = es.size(); i-- > 0;) {
            if (es[i].chunk >= idx.size() || static_cast<std::int64_t>(idx[es[i].chunk].first_ts) > target) continue;
            const auto state = data::read_state(ckp, es[i]);
            if (!b.load(state.data(), state.size())) return std::fprintf(stderr, "corrupt checkpoint\n"), 1;
            start = es[i].chunk;
            rd.seek(start);
            break;
        }
    }
    book::ItchApply<book::TickBook> ap{b};
    data::Record rec{};
    std::uint64_t replayed = 0, last_ts = 0;
    while (rd.next(rec)) {
        const std::uint64_t ts = itch::detail::read_header(rec.data).timestamp;
        if (static_cast<std::int64_t>(ts) > target) break;
        ap.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, ap);
        last_ts = ts, ++replayed;
    }
    std::printf("%s at %s: %zu orders resting; last message %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%09" PRIu64
                "; replayed %" PRIu64 " messages from %s\n",
                pos[1], pos[2], b.order_count(), last_ts / 3'600'000'000'000, last_ts / 60'000'000'000 % 60,
                last_ts / 1'000'000'000 % 60, last_ts % 1'000'000'000, replayed,
                start ? ("the checkpoint before chunk " + std::to_string(start)).c_str() : "the open");
    std::printf("  side     price       shares\n");
    print_side(b, book::Side::Sell, levels);
    print_side(b, book::Side::Buy, levels);
    return 0;
}
