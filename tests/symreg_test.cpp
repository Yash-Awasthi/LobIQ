#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "core/philox.hpp"
#include "strategy/symreg.hpp"

using namespace hft;
using namespace hft::strategy::symreg;

TEST(SymReg, PostfixSubtreesAndText) {
    // (x0 * x1) + tanh(x2) in postfix.
    const Program p = {{Op::Var, 0}, {Op::Var, 1}, {Op::Mul}, {Op::Var, 2}, {Op::Tanh}, {Op::Add}};
    EXPECT_EQ(subtree_start(p, 2), 0u);
    EXPECT_EQ(subtree_start(p, 4), 3u);
    EXPECT_EQ(subtree_start(p, 5), 0u);
    const std::vector<std::string> names = {"a", "b", "c"};
    EXPECT_EQ(to_string(p, names), "((a * b) + tanh(c))");
}

TEST(SymReg, LinearScalingFitsScaleAndOffsetExactly) {
    std::vector<double> x(100), y(100);
    for (int i = 0; i < 100; ++i) x[i] = i * 0.1, y[i] = 3 - 2 * x[i] * x[i];
    Data d({x}, y, 25);
    Formula f;
    f.prog = {{Op::Var, 0}, {Op::Var, 0}, {Op::Mul}};
    d.score(f);
    EXPECT_NEAR(f.a, 3, 1e-9);
    EXPECT_NEAR(f.b, -2, 1e-9);
    EXPECT_NEAR(f.mse, 0, 1e-9);
}

// A product of two of five features, plus noise, is found exactly at its size (3 nodes).
TEST(SymReg, EvolutionFindsAPlantedInteraction) {
    const rng::Stream r(3, 0, 0);
    const std::size_t n = 2000;
    std::vector<std::vector<double>> x(5, std::vector<double>(n));
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < 5; ++j) x[j][i] = 2 * r.uniform(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j)) - 1;
        y[i] = 0.5 + 1.5 * x[1][i] * x[3][i] + 0.05 * (r.uniform(static_cast<std::uint32_t>(i), 9) - 0.5);
    }
    Data d(x, y, 25);
    Params p;
    p.generations = 30;
    const auto hof = evolve(d, p);
    ASSERT_TRUE(std::isfinite(hof[3].mse));
    const std::vector<std::string> names = {"x0", "x1", "x2", "x3", "x4"};
    const std::string s = to_string(hof[3].prog, names);
    EXPECT_TRUE(s == "(x1 * x3)" || s == "(x3 * x1)") << s;
    EXPECT_LT(1 - hof[3].mse * static_cast<double>(n) / d.sst(), 1.0);
    EXPECT_GT(1 - hof[3].mse * static_cast<double>(n) / d.sst(), 0.99);
    for (std::size_t k = 4; k < hof.size(); ++k) {  // larger formulas cannot be much better than the truth
        if (std::isfinite(hof[k].mse)) {
            EXPECT_GT(hof[k].mse, 0.9 * hof[3].mse);
        }
    }
}
