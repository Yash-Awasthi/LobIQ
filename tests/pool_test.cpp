#include "core/pool.hpp"

#include <gtest/gtest.h>

#include <cstring>
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

// Growth moves pages with mremap, splitting off the first 2 MB when a pool outgrows 4 KB pages;
// odd element sizes leave a partial last page. Every step must keep old contents and zero new ones.
template <std::size_t N>
struct Blob {
    std::uint8_t b[N];
};

template <std::size_t N>
void grow_step_by_step(bool huge) {
    const bool was = PoolStats::huge_pages;
    PoolStats::huge_pages = huge;
    Pool<Blob<N>> p(1);
    std::size_t filled = 0;
    while (p.capacity() * N < (9u << 20)) {
        for (std::size_t i = filled; i < p.capacity(); ++i) {
            for (std::size_t k = 0; k < N; ++k) ASSERT_EQ(p[i].b[k], 0) << "new element not zero";
            std::memset(p[i].b, static_cast<int>(i * 31 + 1), N);
        }
        filled = p.capacity();
        p.reserve(filled + 1);
        for (std::size_t i = 0; i < filled; i += 997)
            ASSERT_EQ(p[i].b[N - 1], static_cast<std::uint8_t>(i * 31 + 1));
    }
    Pool<Blob<N>> q(std::move(p));
    q.reserve(q.capacity() + 1);
    EXPECT_EQ(q[filled - 1].b[0], static_cast<std::uint8_t>((filled - 1) * 31 + 1));
    PoolStats::huge_pages = was;
}

TEST(Pool, GrowthAcrossTheHugePageSizeKeepsContents) {
    grow_step_by_step<16>(true);    // crosses at exactly 2 MB
    grow_step_by_step<24>(true);    // partial last page
    grow_step_by_step<4136>(true);  // radix page size
    grow_step_by_step<24>(false);
}
