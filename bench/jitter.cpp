// Spins on the TSC and counts gaps: how often something (interrupt, tick, firmware, power
// management) takes the core away. Run pinned: taskset -c 4 jitter [seconds].

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "core/tsc.hpp"

int main(int argc, char** argv) {
    const double secs = argc > 1 ? std::atof(argv[1]) : 10;
    if (secs <= 0) {
        std::fprintf(stderr, "usage: jitter [seconds]\n");
        return 2;
    }
    const double tpn = hft::tsc::ticks_per_ns();
    constexpr double kEdge[] = {1e3, 2e3, 5e3, 1e4, 2e4, 5e4};  // ns
    std::uint64_t n[6] = {}, max = 0;
    const std::uint64_t end = hft::tsc::start() + static_cast<std::uint64_t>(secs * 1e9 * tpn);
    for (std::uint64_t prev = hft::tsc::start(), now; (now = hft::tsc::start()) < end; prev = now) {
        const std::uint64_t g = now - prev;
        max = g > max ? g : max;
        for (int k = 5; k >= 0; --k)
            if (static_cast<double>(g) / tpn > kEdge[k]) {
                ++n[k];
                break;
            }
    }
    std::printf("seconds %.0f gaps 1-2us %llu 2-5us %llu 5-10us %llu 10-20us %llu 20-50us %llu >50us %llu max %.1f us\n",
                secs, (unsigned long long)n[0], (unsigned long long)n[1], (unsigned long long)n[2],
                (unsigned long long)n[3], (unsigned long long)n[4], (unsigned long long)n[5], max / tpn / 1e3);
}
