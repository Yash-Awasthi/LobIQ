#include <gtest/gtest.h>

#include "strategy/microstructure.hpp"
#include "strategy/mlofi.hpp"

using namespace hft;
namespace ml = strategy::mlofi;

namespace {

ml::Levels top(std::uint32_t bid, std::uint64_t bq, std::uint32_t ask, std::uint64_t aq) {
    ml::Levels l;
    l.bid_px[0] = bid, l.bid_q[0] = bq, l.ask_px[0] = ask, l.ask_q[0] = aq;
    return l;
}

}  // namespace

// Level 1 is the best-level OFI of Cont, Kukanov and Stoikov (microstructure.hpp), case by case.
TEST(MultiLevelOfi, LevelOneIsTheBestLevelOfi) {
    const std::array<std::array<std::uint64_t, 4>, 6> cases = {{{100, 5, 101, 7}, {100, 9, 101, 7}, {99, 3, 101, 7},
                                                                {101, 4, 102, 2}, {100, 5, 100, 1}, {98, 8, 103, 6}}};
    for (const auto& a : cases)
        for (const auto& b : cases) {
            const auto pa = top(a[0], a[1], a[2], a[3]), pb = top(b[0], b[1], b[2], b[3]);
            std::array<double, ml::kLevels> acc{};
            ml::add_ofi(pa, pb, acc);
            const book::Bbo x{static_cast<std::uint32_t>(a[0]), static_cast<std::uint32_t>(a[2]), a[1], a[3]};
            const book::Bbo y{static_cast<std::uint32_t>(b[0]), static_cast<std::uint32_t>(b[2]), b[1], b[3]};
            EXPECT_EQ(acc[0], static_cast<double>(strategy::micro::ofi_event(x, y)));
            EXPECT_EQ(acc[1], 0);  // empty deeper levels contribute nothing
        }
}

TEST(MultiLevelOfi, NewAskLiquidityIsSellingPressure) {
    ml::Levels prev = top(100, 5, 0, 0), cur = top(100, 5, 101, 40);
    std::array<double, ml::kLevels> acc{};
    ml::add_ofi(prev, cur, acc);
    EXPECT_EQ(acc[0], -40);  // bid unchanged: +5 - 5; ask appears: -40
}

// When every level moves with one common factor, the first component weights them equally and
// explains almost all the variance; when only level 1 moves, it is level 1.
TEST(MultiLevelOfi, FirstComponentFindsTheCommonFactor) {
    std::vector<std::array<double, ml::kLevels>> common, single;
    for (int t = 0; t < 200; ++t) {
        const double f = std::sin(t * 0.37) * 10;
        std::array<double, ml::kLevels> r{}, s{};
        for (int m = 0; m < ml::kLevels; ++m) r[m] = f + 0.01 * ((t * 7 + m * 13) % 5 - 2);
        s[0] = -f;
        common.push_back(r), single.push_back(s);
    }
    double share = 0;
    const auto w = ml::first_pc(common, &share);
    for (const double x : w) EXPECT_NEAR(x, 0.1, 1e-3);
    EXPECT_GT(share, 0.99);
    const auto v = ml::first_pc(single);
    EXPECT_NEAR(v[0], 1, 1e-9);
}

TEST(MultiLevelOfi, RidgeRecoversAnExactFit) {
    std::vector<std::vector<double>> X;
    std::vector<double> y;
    for (int t = 0; t < 50; ++t) {
        const double a = t % 7, b = (t * 3) % 11;
        X.push_back({a, b}), y.push_back(2 + 3 * a - b);
    }
    const auto c = ml::ridge(X, y, 0);
    EXPECT_NEAR(c[0], 2, 1e-9);
    EXPECT_NEAR(c[1], 3, 1e-9);
    EXPECT_NEAR(c[2], -1, 1e-9);
    EXPECT_NEAR(ml::predict(c, {1, 1}), 4, 1e-9);
    EXPECT_LT(std::fabs(ml::ridge(X, y, 1e6)[1]), 0.1);  // a heavy penalty shrinks the slopes
}
