#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "core/philox.hpp"
#include "strategy/regime.hpp"

using namespace lobiq;
using namespace lobiq::strategy::regime;

namespace {

// Two sticky states; feature 0 has mean 0 or 3, feature 1 is noise in both.
std::vector<std::size_t> simulate(Seq& s, std::size_t n, std::uint64_t seed) {
    const rng::Stream r(seed, 0, 0);
    std::vector<std::size_t> state(n);
    std::size_t k = 0;
    for (std::size_t t = 0; t < n; ++t) {
        const auto i = static_cast<std::uint32_t>(t);
        if (t && r.uniform(i, 0) < 0.05) k = 1 - k;
        state[t] = k;
        auto g = [&](std::uint32_t p) {
            return std::sqrt(-2 * std::log(r.uniform(i, p, 0))) * std::cos(2 * std::numbers::pi * r.uniform(i, p, 1));
        };
        s.push_back({3.0 * static_cast<double>(k) + g(1), g(2)});
    }
    return state;
}

}  // namespace

TEST(KMeans, SeparatesTwoBlobs) {
    std::vector<Row> rows;
    for (int i = 0; i < 100; ++i) rows.push_back({0.01 * i, 0}), rows.push_back({10 + 0.01 * i, 5});
    std::vector<const Row*> x;
    for (const Row& r : rows) x.push_back(&r);
    const KMeans km = kmeans(x, 2);
    for (std::size_t i = 0; i < rows.size(); i += 2) EXPECT_NE(km.label[i], km.label[i + 1]);
}

TEST(Hmm, BaumWelchRecoversTwoStickyStates) {
    std::vector<Seq> seqs(4);
    std::vector<std::vector<std::size_t>> truth;
    for (std::size_t i = 0; i < seqs.size(); ++i) truth.push_back(simulate(seqs[i], 1000, 20 + i));
    Hmm h = fit_hmm(seqs, 2);
    ASSERT_TRUE(h.ok);
    sort_states(h, 0);
    EXPECT_NEAR(h.mean[0 * 2 + 0], 0, 0.15);
    EXPECT_NEAR(h.mean[1 * 2 + 0], 3, 0.15);
    EXPECT_NEAR(h.A[0], 0.95, 0.03);
    EXPECT_NEAR(h.A[3], 0.95, 0.03);
    double right = 0, n = 0;
    for (std::size_t i = 0; i < seqs.size(); ++i) {
        const auto path = viterbi(h, seqs[i]);
        for (std::size_t t = 0; t < path.size(); ++t) right += path[t] == truth[i][t], n += 1;
    }
    EXPECT_GT(right / n, 0.95);
}

// A filtered label never depends on later data.
TEST(Hmm, FilterIsCausal) {
    Seq s;
    simulate(s, 300, 31);
    const Hmm h = fit_hmm({s}, 2);
    ASSERT_TRUE(h.ok);
    const auto full = filter(h, s);
    const Seq prefix(s.begin(), s.begin() + 150);
    const auto part = filter(h, prefix);
    for (std::size_t t = 0; t < part.size(); ++t)
        for (std::size_t k = 0; k < 2; ++k) EXPECT_DOUBLE_EQ(part[t][k], full[t][k]);
}

TEST(Hmm, MoreStatesOnOneStateDataDoNotWinOnBic) {
    Seq s;
    const rng::Stream r(5, 0, 0);
    for (std::uint32_t t = 0; t < 2000; ++t)
        s.push_back({std::sqrt(-2 * std::log(r.uniform(t, 0))) * std::cos(2 * std::numbers::pi * r.uniform(t, 1))});
    const Hmm one = fit_hmm({s}, 1), three = fit_hmm({s}, 3);
    ASSERT_TRUE(one.ok && three.ok);
    const double n = std::log(2000.0);
    EXPECT_LT(-2 * one.loglik + one.params() * n, -2 * three.loglik + three.params() * n);
}
