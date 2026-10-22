// Writes stored ITCH days in LOBSTER's format (src/data/lobster.hpp): per symbol,
// SYMBOL_YYYY-MM-DD_START_END_message_N.csv and ..._orderbook_N.csv, START and END in milliseconds
// after midnight (34200000_57600000, 09:30 to 16:00 ET, by default), one order book line per message
// line. The books are built from the day's first message; orders resting at START keep the ids they
// were submitted with before it, as in LOBSTER's own files.
// Usage: itch_lobster <store> <SYMBOL,SYMBOL,...> <out-dir> [--levels N] [--from HH:MM]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "book/tick_book.hpp"
#include "core/parallel.hpp"
#include "data/lobster.hpp"
#include "data/store.hpp"
#include "feed/itch.hpp"

namespace {

using namespace hft;
namespace lb = data::lobster;

constexpr std::uint64_t kNsPerMin = 60'000'000'000ull, kEnd = 16 * 60 * kNsPerMin;

// "store-S121025-v50" -> "2025-12-10"; otherwise the directory name.
std::string date_of(const std::filesystem::path& dir) {
    const std::string n = dir.filename().string();
    std::smatch m;
    if (std::regex_search(n, m, std::regex("S(\\d{2})(\\d{2})(\\d{2})"))) return "20" + m[3].str() + "-" + m[1].str() + "-" + m[2].str();
    return n;
}

std::uint64_t write_symbol(const std::filesystem::path& dir, std::uint16_t loc, const std::string& stem, int levels, std::uint64_t start) {
    std::FILE* msg = std::fopen((stem + "_message_" + std::to_string(levels) + ".csv").c_str(), "w");
    std::FILE* ob = std::fopen((stem + "_orderbook_" + std::to_string(levels) + ".csv").c_str(), "w");
    if (!msg || !ob) {
        if (msg) std::fclose(msg);
        if (ob) std::fclose(ob);
        throw std::runtime_error("cannot write " + stem + "_*.csv");
    }
    book::TickBook b;
    std::uint64_t ts = 0, lines = 0;
    auto emit = [&](const lb::Event& e) {
        if (ts < start || ts >= kEnd) return;
        std::fputs(lb::message_line(ts, e).c_str(), msg);
        std::fputs(lb::book_line(b, levels).c_str(), ob);
        ++lines;
    };
    lb::Translator<decltype(emit)> tr(b, emit);
    data::SymbolReader rd(dir, loc);
    data::Record rec{};
    while (rd.next(rec)) {
        ts = itch::detail::read_header(rec.data).timestamp;
        if (ts >= kEnd) break;
        tr.seq = rec.seq;
        itch::dispatch(rec.data, rec.len, tr);
    }
    std::fclose(msg);
    std::fclose(ob);
    return lines;
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* usage = "usage: itch_lobster <store> <SYMBOL,SYMBOL,...> <out-dir> [--levels N] [--from HH:MM]";
    int levels = 10;
    std::uint64_t start = (9 * 60 + 30) * kNsPerMin;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--levels") == 0 && i + 1 < argc) levels = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            unsigned h, m;
            if (std::sscanf(argv[++i], "%2u:%2u", &h, &m) != 2 || h > 15 || m > 59) return std::fprintf(stderr, "%s\n", usage), 2;
            start = (static_cast<std::uint64_t>(h) * 60 + m) * kNsPerMin;
        } else if (argv[i][0] == '-') return std::fprintf(stderr, "%s\n", usage), 2;
        else pos.push_back(argv[i]);
    }
    if (pos.size() != 3 || levels < 1 || levels > 50) return std::fprintf(stderr, "%s\n", usage), 2;
    const std::filesystem::path dir = pos[0], out = pos[2];
    const auto names = data::read_symbols(dir);
    std::vector<std::string> syms;
    std::stringstream ss(pos[1]);
    for (std::string s; std::getline(ss, s, ',');)
        if (!s.empty()) syms.push_back(s);
    if (syms.empty()) return std::fprintf(stderr, "%s\n", usage), 2;
    for (const std::string& s : syms)
        if (!names.contains(s)) return std::fprintf(stderr, "symbol %s not in %s\n", s.c_str(), dir.c_str()), 2;
    std::filesystem::create_directories(out);
    const std::string window = date_of(dir) + "_" + std::to_string(start / 1'000'000) + "_" + std::to_string(kEnd / 1'000'000);
    std::vector<std::uint64_t> counts(syms.size());
    std::string error;
    std::mutex error_lock;
    hft::parallel_for(syms.size(), [&](std::size_t i) {
        try {
            counts[i] = write_symbol(dir, names.at(syms[i]), (out / (syms[i] + "_" + window)).string(), levels, start);
        } catch (const std::exception& e) {
            const std::lock_guard g(error_lock);
            error = e.what();
        }
    });
    if (!error.empty()) return std::fprintf(stderr, "error: %s\n", error.c_str()), 1;
    for (std::size_t i = 0; i < syms.size(); ++i)
        std::printf("%s\t%llu events\t%s_%s_{message,orderbook}_%d.csv\n", syms[i].c_str(), static_cast<unsigned long long>(counts[i]),
                    syms[i].c_str(), window.c_str(), levels);
    return 0;
}
