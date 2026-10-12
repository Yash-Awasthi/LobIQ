#pragma once

// Runs f(i) for every i in [0, n) on THREADS threads (default: all hardware threads, never more than
// n), the calling thread included. Items are handed out one at a time, so uneven items balance.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include <vector>

namespace hft {

template <class F>
void parallel_for(std::size_t n, F&& f) {
    const char* env = std::getenv("THREADS");
    const std::size_t want = env ? std::strtoul(env, nullptr, 10) : std::thread::hardware_concurrency();
    const std::size_t threads = std::max<std::size_t>(1, std::min(want, n));
    std::atomic<std::size_t> next{0};
    auto worker = [&] {
        for (std::size_t i; (i = next.fetch_add(1)) < n;) f(i);
    };
    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

}  // namespace hft
