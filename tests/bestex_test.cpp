#include "surveil/best_ex.hpp"

#include <gtest/gtest.h>

#include "itch_writer.hpp"

using hft::surveil::BestEx;

namespace {
constexpr std::uint64_t kTen = 36'000'000'000'000, kSec = 1'000'000'000;
void feed(BestEx& b, ItchWriter& w) { b.on_message(w.b.data(), w.b.size()); }
}  // namespace

TEST(BestEx, SplitsTheEffectiveSpreadAndCountsPriceImprovement) {
    BestEx b;
    ItchWriter w;
    w.ts = kTen;
    feed(b, w.add(1, 'B', 500, 1'000'000));  // $100.00
    feed(b, w.add(2, 'S', 500, 1'000'200));  // $100.02, mid $100.01
    w.ts = kTen + kSec;
    feed(b, w.exec(2, 100));                 // a taker buys 100 at the ask: 1 cent over the mid
    w.ts = kTen + 2 * kSec;
    feed(b, w.del(1));
    feed(b, w.del(2));
    feed(b, w.add(3, 'B', 500, 1'000'200));  // $100.02
    feed(b, w.add(4, 'S', 500, 1'000'400));  // $100.04, mid $100.03
    w.ts = kTen + 3 * kSec;
    feed(b, w.hidden('B', 100, 1'000'300));  // hidden at the mid: half the spread better than the quote
    feed(b, w.hidden('B', 200, 1'000'350));  // hidden buy at $100.035: half a cent under the ask
    feed(b, w.hidden('B', 50, 1'000'400));   // at the ask: not inside the quote, no improvement
    w.ts = kTen + 9 * kSec;
    feed(b, w.add(5, 'B', 100, 999'000));    // any later message resolves the 5 s markouts
    const auto& small = b.buckets()[0];       // 1-100 shares
    const auto& mid = b.buckets()[1];         // 101-500 shares
    EXPECT_DOUBLE_EQ(small.execs, 2);         // the displayed 100 and the hidden 100 at the mid
    EXPECT_DOUBLE_EQ(small.shares, 200);
    EXPECT_NEAR(small.eff, 1.00, 1e-9);       // 100 x $0.01
    EXPECT_NEAR(small.real, -1.00, 1e-9);     // the mid went to $100.03: the seller lost $0.01 a share
    EXPECT_NEAR(small.impact, 2.00, 1e-9);
    EXPECT_NEAR(small.eff, small.real + small.impact, 1e-9);
    EXPECT_DOUBLE_EQ(small.hidden_shares, 100);
    EXPECT_NEAR(small.improved, 1.00, 1e-9);  // 100 x $0.01
    EXPECT_DOUBLE_EQ(mid.hidden_shares, 200);
    EXPECT_NEAR(mid.improved, 1.00, 1e-9);    // 200 x $0.005
    EXPECT_NEAR(mid.eff, 200 * 0.005, 1e-9);  // a buy half a cent over the mid
}

TEST(BestEx, SignsTakerSells) {
    BestEx b;
    ItchWriter w;
    w.ts = kTen;
    feed(b, w.add(1, 'B', 500, 1'000'000));  // $100.00
    feed(b, w.add(2, 'S', 500, 1'000'200));  // $100.02
    w.ts = kTen + kSec;
    feed(b, w.exec(1, 100));                 // a taker sells 100 at the bid: 1 cent under the mid
    w.ts = kTen + 2 * kSec;
    feed(b, w.add(3, 'S', 500, 1'000'100));  // ask $100.01, mid $100.005
    w.ts = kTen + 9 * kSec;
    feed(b, w.add(4, 'B', 100, 990'000));
    const auto& s = b.buckets()[0];
    EXPECT_NEAR(s.eff, 1.00, 1e-9);           // a taker sell pays the half spread too
    EXPECT_NEAR(s.impact, 0.50, 1e-9);        // the mid fell half a cent, with the seller
    EXPECT_NEAR(s.real, 0.50, 1e-9);
}
