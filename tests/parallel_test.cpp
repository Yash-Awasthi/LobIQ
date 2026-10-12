#include "core/parallel.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

TEST(ParallelFor, RunsEveryItemExactlyOnce) {
    for (const std::size_t n : {0, 1, 7, 10'000}) {
        std::vector<std::atomic<int>> seen(n);
        hft::parallel_for(n, [&](std::size_t i) { seen[i]++; });
        for (std::size_t i = 0; i < n; ++i) EXPECT_EQ(seen[i].load(), 1) << i;
    }
}
