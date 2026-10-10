// Synthetic ITCH session for one symbol (QRSIM, locate 1) from the queue-reactive model, as a
// BinaryFILE stream on stdout. Usage: gen_day <seed> [minutes] | ingest <store-dir>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sources/queue_reactive.hpp"

int main(int argc, char** argv) {
    auto digits = [](const char* s) { return *s && std::string(s).find_first_not_of("0123456789") == std::string::npos; };
    if (argc < 2 || argc > 3 || !digits(argv[1]) || (argc == 3 && !digits(argv[2])) || ::isatty(STDOUT_FILENO)) {
        std::fprintf(stderr, "usage: gen_day <seed> [minutes, default 390] | ingest <store-dir>\n");
        return 2;
    }
    const auto minutes = argc == 3 ? std::strtoull(argv[2], nullptr, 10) : 390;
    if (minutes == 0 || minutes > 390) {
        std::fprintf(stderr, "minutes must be 1 to 390\n");
        return 2;
    }
    hft::sources::QrParams p;
    p.K = 3, p.N = 20, p.theta = 0.6;
    const auto cells = static_cast<std::size_t>(p.K * (p.N + 1));
    p.L.assign(cells, 0), p.C.assign(cells, 0), p.M.assign(cells, 0), p.init.assign(cells, 0);
    for (int i = 0; i < p.K; ++i)
        for (int n = 0; n <= p.N; ++n) {
            const auto c = static_cast<std::size_t>(i * (p.N + 1) + n);
            p.L[c] = 2.0 / (1 + i);  // insertions slow down away from the best
            p.C[c] = 0.3 * n;        // each resting order cancels at the same rate
            p.M[c] = i == 0 ? 0.8 : 0.2;
            p.init[c] = n >= 2 && n <= 6 ? 0.2 : 0.0;
        }
    p.end_ns = p.start_ns + minutes * 60'000'000'000ull;
    std::vector<std::uint8_t> out;
    hft::sources::QueueReactive sim(p, std::strtoull(argv[1], nullptr, 10));
    sim.day(out);
    if (std::fwrite(out.data(), 1, out.size(), stdout) != out.size()) return 1;
    std::fprintf(stderr, "%llu events, %zu bytes\n", static_cast<unsigned long long>(sim.events()), out.size());
    return 0;
}
