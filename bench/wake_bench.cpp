// Doorbell wake-up latency: one thread rings after a pause, another sleeps on the futex or
// spins, and the gap from ring to the consumer running is timed with the TSC.
// Usage: wake_bench <producer-cpu> <consumer-cpu> <futex|spin> [samples] [gap-us]

#include <pthread.h>
#include <sched.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "core/doorbell.hpp"
#include "core/histogram.hpp"
#include "core/tsc.hpp"

namespace {

void pin(int cpu) {
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    if (pthread_setaffinity_np(pthread_self(), sizeof s, &s) != 0) {
        std::fprintf(stderr, "cannot pin to cpu %d\n", cpu);
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || (std::strcmp(argv[3], "futex") != 0 && std::strcmp(argv[3], "spin") != 0)) {
        std::fprintf(stderr, "usage: wake_bench <producer-cpu> <consumer-cpu> <futex|spin> [samples] [gap-us]\n");
        return 2;
    }
    const int pcpu = std::atoi(argv[1]), ccpu = std::atoi(argv[2]);
    const bool spin = std::strcmp(argv[3], "spin") == 0;
    const long samples = argc > 4 ? std::atol(argv[4]) : 100'000;
    const long gap_us = argc > 5 ? std::atol(argv[5]) : 100;
    const double tpn = hft::tsc::ticks_per_ns();

    hft::Doorbell bell;
    std::atomic<std::uint64_t> stamp{0};
    std::atomic<long> done{0};
    hft::Histogram h;

    std::thread consumer([&] {
        pin(ccpu);
        for (long i = 0; i < samples; ++i) {
            const std::uint32_t seen = bell.snapshot();
            done.store(i + 1);
            if (spin) {
                while (bell.snapshot() == seen) __builtin_ia32_pause();
            } else {
                while (bell.snapshot() == seen) bell.wait(seen, 1'000'000);
            }
            const std::uint64_t now = hft::tsc::stop();
            h.record(now - stamp.load());
        }
    });

    pin(pcpu);
    const std::uint64_t gap = static_cast<std::uint64_t>(static_cast<double>(gap_us) * 1000 * tpn);
    std::uint64_t x = 88172645463325252ull;
    for (long i = 0; i < samples; ++i) {
        while (done.load() != i + 1) __builtin_ia32_pause();
        // Gap varies 50-150% so the consumer cannot settle into a rhythm.
        x ^= x << 13, x ^= x >> 7, x ^= x << 17;
        const std::uint64_t until = hft::tsc::start() + gap / 2 + x % (gap + 1);
        while (hft::tsc::start() < until) __builtin_ia32_pause();
        stamp.store(hft::tsc::start());
        bell.ring();
    }
    consumer.join();

    const double ps[] = {50, 99, 99.9, 99.99};
    std::uint64_t v[4];
    h.percentiles(ps, v);
    std::printf("mode,producer,consumer,samples,gap_us,p50_ns,p99_ns,p999_ns,p9999_ns,max_ns\n");
    std::printf("%s,%d,%d,%ld,%ld,%.0f,%.0f,%.0f,%.0f,%.0f\n", argv[3], pcpu, ccpu, samples, gap_us,
                v[0] / tpn, v[1] / tpn, v[2] / tpn, v[3] / tpn, static_cast<double>(h.max()) / tpn);
}
