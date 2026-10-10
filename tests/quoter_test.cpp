#include <gtest/gtest.h>

#include <memory>

#include "pm/maker.hpp"

using namespace hft::pm;

namespace {

MakerParams params() {
    MakerParams p;
    p.warmup_s = 0;
    p.requote_s = 0;
    return p;
}

// Bids 0.49 (500 shares) and 0.48, asks 0.51 (500) and 0.52.
void seed(TokenMaker& m) {
    m.book.set(true, 4900, 500);
    m.book.set(true, 4800, 500);
    m.book.set(false, 5100, 500);
    m.book.set(false, 5200, 500);
    m.on_snapshot(1'000'000'000);
}

// Asks for fixed prices, whatever the market does.
struct Fixed final : Quoter {
    Quote q;
    explicit Fixed(Quote w) : q(w) {}
    Quote quote(const QuoteInput&) override { return q; }
};

std::unique_ptr<TokenMaker> with(Quote w, MakerParams p = params()) {
    auto m = std::make_unique<TokenMaker>(p);
    m->quoter = std::make_unique<Fixed>(w);
    seed(*m);
    return m;
}

}  // namespace

TEST(PmQuoter, LogitAsPluginMatchesTheBuiltIn) {
    const QuoterEntry* q = find_quoter("logit_as");
    ASSERT_NE(q, nullptr);
    TokenMaker a(params()), b(params());
    b.quoter = q->make();
    seed(a), seed(b);
    for (int i = 0; i < 50; ++i) {  // the mid walks and inventory builds, so both terms matter
        const std::int64_t ns = 2'000'000'000LL + i * 700'000'000LL;
        for (TokenMaker* m : {&a, &b}) {
            m->book.set(true, 4900 - 100 * (i % 3), 400 + 10 * i);
            m->on_trade(ns, i % 4 == 0, m->bid_px(), 50);
        }
        ASSERT_EQ(a.bid_px(), b.bid_px()) << i;
        ASSERT_EQ(a.ask_px(), b.ask_px()) << i;
        ASSERT_DOUBLE_EQ(a.inventory(), b.inventory()) << i;
    }
    EXPECT_NE(a.inventory(), 0.0);
}

TEST(PmQuoter, PricesAreKeptOnTheGridAndNeverCross) {
    const auto m = with({5250, 4750});  // bid above the best ask, ask below the best bid
    EXPECT_EQ(m->bid_px(), 5100 - 100);
    EXPECT_EQ(m->ask_px(), 4900 + 100);
    const auto g = with({4777, 5333});  // off the grid: the bid floors, the ask ceils
    EXPECT_EQ(g->bid_px(), 4700);
    EXPECT_EQ(g->ask_px(), 5400);
}

TEST(PmQuoter, NegativePricesAndTheInventoryCapLeaveASideUnquoted) {
    const auto m = with({-1, 5300});
    EXPECT_LT(m->bid_px(), 0);
    EXPECT_EQ(m->ask_px(), 5300);
    auto p = params();
    p.max_inv = 50;
    const auto c = with({4800, 5300}, p);
    c->on_trade(2'000'000'000, false, 4700, 1);  // trades through our bid: long 100, past the cap
    EXPECT_LT(c->bid_px(), 0);
    EXPECT_EQ(c->ask_px(), 5300);
}
