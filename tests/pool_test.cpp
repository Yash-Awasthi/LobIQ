#include "core/pool.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using hft::Pool;
using hft::PoolStats;

TEST(Pool, GrowthKeepsContents) {
    Pool<std::uint64_t> p(4);
    for (std::uint64_t i = 0; i < 4; ++i) p[i] = i * 7;
    p.reserve(100'000);
    for (std::uint64_t i = 0; i < 4; ++i) EXPECT_EQ(p[i], i * 7);
}

// Sweeps and book_replay build pools on several threads at once; under TSan this
// fails if the shared mapping counters are not updated atomically.
TEST(Pool, PoolsGrowOnSeveralThreads) {
    const std::uint64_t maps = PoolStats::maps;
    std::vector<std::thread> t;
    for (int i = 0; i < 4; ++i)
        t.emplace_back([] {
            for (int k = 0; k < 50; ++k) {
                Pool<std::uint32_t> p(16);
                p.reserve(4096);
            }
        });
    for (std::thread& x : t) x.join();
    EXPECT_EQ(PoolStats::maps - maps, 4u * 50 * 2);
}
