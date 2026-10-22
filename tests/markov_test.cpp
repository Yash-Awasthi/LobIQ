#include <gtest/gtest.h>

#include "strategy/markov.hpp"

using hft::book::Bbo;
using hft::strategy::MarkovBook;

namespace {

Bbo bbo(std::uint32_t bid, std::uint32_t ask, std::uint64_t bq, std::uint64_t aq) { return Bbo{bid, ask, bq, aq}; }

}  // namespace

TEST(Markov, StatesAreSpreadAndBidShare) {
    const MarkovBook m(5, 3, 100);
    EXPECT_EQ(m.states(), 15);
    EXPECT_EQ(m.state(bbo(10000, 10100, 100, 100)), 2);   // 1 tick, share 0.5
    EXPECT_EQ(m.state(bbo(10000, 10200, 900, 100)), 9);   // 2 ticks, share 0.9
    EXPECT_EQ(m.state(bbo(10000, 10700, 1, 99)), 10);     // 7 ticks capped at 3, share 0.01
    EXPECT_EQ(m.state(bbo(10000, 10000, 100, 100)), -1);   // locked
    EXPECT_EQ(m.state(bbo(0, 10100, 0, 100)), -1);         // one-sided
}

// A heavy bid always precedes an up move and a heavy ask a down move: fitted on one pass, the
// chain calls every move of the next, where always guessing one direction is right half the time.
TEST(Markov, CountsPredictTheTestPeriod) {
    MarkovBook m(5, 3, 100);
    const Bbo cycle[] = {bbo(10000, 10100, 900, 100), bbo(10100, 10200, 500, 500), bbo(10100, 10200, 100, 900),
                         bbo(10000, 10100, 500, 500)};
    std::uint64_t ts = 0;
    for (int fit = 1; fit >= 0; --fit)
        for (int r = 0; r < 10; ++r)
            for (const Bbo& b : cycle) m.observe(b, ++ts, fit == 1);
    EXPECT_EQ(m.ups(4), 10u);    // heavy bid, then up
    EXPECT_EQ(m.downs(0), 10u);  // heavy ask, then down
    EXPECT_EQ(m.downs(4), 0u);
    EXPECT_EQ(m.test_moves(), 20u);
    EXPECT_EQ(m.test_hits(), 20u);
    EXPECT_DOUBLE_EQ(m.baseline(), 0.5);
    EXPECT_EQ(m.transitions(4, 2), 20u);
    EXPECT_EQ(m.time_ns(4) + m.time_ns(2) + m.time_ns(0), ts - 1);
}

TEST(Markov, AOneSidedBookBreaksTheChain) {
    MarkovBook m(5, 3, 100);
    m.observe(bbo(10000, 10100, 900, 100), 1, true);
    m.observe(bbo(0, 10100, 0, 100), 2, true);
    m.observe(bbo(10100, 10200, 500, 500), 3, true);  // mid moved, but not from a known state
    EXPECT_EQ(m.ups(4), 0u);
    EXPECT_EQ(m.transitions(4, 2), 0u);
}
