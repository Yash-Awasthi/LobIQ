#pragma once

// Symbolic regression by genetic programming (Koza 1992), with what makes it usable on noisy
// targets: linear scaling of every candidate (Keijzer 2003), so the search looks for shape and not
// for constants; and a hall of fame of the best formula at each size, the accuracy-complexity
// Pareto front that PySR reports (Cranmer 2023). docs/METHODS.md "Symbolic regression".
//
// Programs are postfix arrays evaluated a whole column at a time over contiguous buffers that are
// allocated once, so scoring a candidate is a few vector passes and no allocation.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/philox.hpp"

namespace hft::strategy::symreg {

enum class Op : std::uint8_t { Var, Const, Add, Sub, Mul, Div, Neg, Tanh };

struct Node {
    Op op;
    std::uint8_t var = 0;
    double c = 0;
};

inline int arity(Op o) { return o == Op::Var || o == Op::Const ? 0 : o == Op::Neg || o == Op::Tanh ? 1 : 2; }

using Program = std::vector<Node>;

// Start of the subtree ending at position `end` of a postfix program.
inline std::size_t subtree_start(const Program& p, std::size_t end) {
    int need = 1;
    std::size_t i = end + 1;
    while (need > 0) need += arity(p[--i].op) - 1;
    return i;
}

struct Formula {
    Program prog;
    double a = 0, b = 0;  // y = a + b f(x)
    double mse = std::numeric_limits<double>::infinity();
    std::size_t size() const { return prog.size(); }
};

// Columns of the feature matrix and the target, with evaluation buffers sized once.
class Data {
   public:
    Data(std::vector<std::vector<double>> cols, std::vector<double> y, std::size_t max_size)
        : cols_(std::move(cols)), y_(std::move(y)), stack_(max_size, std::vector<double>(y_.size())) {
        n_ = static_cast<double>(y_.size());
        for (const double v : y_) my_ += v / n_;
        for (const double v : y_) syy_ += (v - my_) * (v - my_);
    }
    std::size_t rows() const { return y_.size(); }
    std::size_t features() const { return cols_.size(); }
    double sst() const { return syy_; }

    // f(x) for every row into the top of the stack; returns that column.
    const std::vector<double>& eval(const Program& p) {
        std::size_t sp = 0;
        const std::size_t n = y_.size();
        for (const Node& nd : p) {
            switch (nd.op) {
                case Op::Var: std::copy(cols_[nd.var].begin(), cols_[nd.var].end(), stack_[sp++].begin()); break;
                case Op::Const: std::fill(stack_[sp].begin(), stack_[sp].end(), nd.c), ++sp; break;
                case Op::Neg:
                    for (double& v : stack_[sp - 1]) v = -v;
                    break;
                case Op::Tanh:
                    for (double& v : stack_[sp - 1]) v = std::tanh(v);
                    break;
                default: {
                    double* l = stack_[sp - 2].data();
                    const double* r = stack_[sp - 1].data();
                    if (nd.op == Op::Add) for (std::size_t i = 0; i < n; ++i) l[i] += r[i];
                    else if (nd.op == Op::Sub) for (std::size_t i = 0; i < n; ++i) l[i] -= r[i];
                    else if (nd.op == Op::Mul) for (std::size_t i = 0; i < n; ++i) l[i] *= r[i];
                    else for (std::size_t i = 0; i < n; ++i) l[i] = std::abs(r[i]) > 1e-6 ? l[i] / r[i] : 0;  // protected
                    --sp;
                }
            }
        }
        return stack_[0];
    }

    // Least-squares a, b of y on f, and the residual MSE; infinite for a non-finite f.
    void score(Formula& f) {
        const std::vector<double>& v = eval(f.prog);
        double mf = 0;
        for (const double x : v) mf += x / n_;
        double sff = 0, sfy = 0;
        for (std::size_t i = 0; i < v.size(); ++i) sff += (v[i] - mf) * (v[i] - mf), sfy += (v[i] - mf) * (y_[i] - my_);
        if (!std::isfinite(sff) || !std::isfinite(sfy)) {
            f.mse = std::numeric_limits<double>::infinity();
            return;
        }
        f.b = sff > 1e-12 ? sfy / sff : 0;
        f.a = my_ - f.b * mf;
        f.mse = (syy_ - f.b * sfy) / n_;
    }

    // Predictions a + b f(x) on these rows.
    std::vector<double> predict(const Formula& f) {
        const std::vector<double>& v = eval(f.prog);
        std::vector<double> out(v.size());
        for (std::size_t i = 0; i < v.size(); ++i) out[i] = f.a + f.b * v[i];
        return out;
    }

   private:
    std::vector<std::vector<double>> cols_;
    std::vector<double> y_;
    std::vector<std::vector<double>> stack_;
    double n_ = 0, my_ = 0, syy_ = 0;
};

struct Params {
    std::size_t population = 400, generations = 40, tournament = 7, max_size = 25;
    double p_crossover = 0.7, p_mutation = 0.25;
    std::uint64_t seed = 1;
};

namespace detail {

struct Rng {
    rng::Stream s;
    std::uint32_t n = 0;
    double u() { return s.uniform(n++, 0); }
    std::size_t pick(std::size_t k) { return std::min(static_cast<std::size_t>(u() * static_cast<double>(k)), k - 1); }
};

inline void grow(Program& p, Rng& r, std::size_t features, int depth, bool full) {
    if (depth <= 0 || (!full && r.u() < 0.3)) {
        if (r.u() < 0.8) p.push_back({Op::Var, static_cast<std::uint8_t>(r.pick(features)), 0});
        else p.push_back({Op::Const, 0, std::round((r.u() * 4 - 2) * 100) / 100});
        return;
    }
    static constexpr Op ops[] = {Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Neg, Op::Tanh};
    const Op o = ops[r.pick(6)];
    for (int k = 0; k < arity(o); ++k) grow(p, r, features, depth - 1, full);
    p.push_back({o, 0, 0});
}

}  // namespace detail

// The hall of fame: hof[s] is the best formula of exactly s nodes found (mse infinite if none).
inline std::vector<Formula> evolve(Data& d, const Params& prm) {
    detail::Rng r{rng::Stream(prm.seed, 0, 0)};
    const std::size_t F = d.features();
    std::vector<Formula> pop(prm.population), next(prm.population), hof(prm.max_size + 1);
    auto keep = [&](const Formula& f) {
        if (f.size() <= prm.max_size && f.mse < hof[f.size()].mse) hof[f.size()] = f;
    };
    for (std::size_t i = 0; i < pop.size(); ++i) {  // ramped half-and-half, depths 1 to 4
        do {
            pop[i].prog.clear();
            detail::grow(pop[i].prog, r, F, 1 + static_cast<int>(i % 4), i % 2 == 0);
        } while (pop[i].size() > prm.max_size);
        d.score(pop[i]), keep(pop[i]);
    }
    auto tournament = [&]() -> const Formula& {
        std::size_t best = r.pick(pop.size());
        for (std::size_t k = 1; k < prm.tournament; ++k) {
            const std::size_t c = r.pick(pop.size());
            // Parsimony: a tie in error goes to the smaller program.
            if (pop[c].mse < pop[best].mse || (pop[c].mse == pop[best].mse && pop[c].size() < pop[best].size())) best = c;
        }
        return pop[best];
    };
    for (std::size_t g = 0; g < prm.generations; ++g) {
        next[0] = *std::min_element(pop.begin(), pop.end(), [](const Formula& a, const Formula& b) { return a.mse < b.mse; });
        for (std::size_t i = 1; i < next.size(); ++i) {
            Program child = tournament().prog;
            const double u = r.u();
            if (u < prm.p_crossover) {  // swap in a random subtree of another parent
                const Program& donor = tournament().prog;
                const std::size_t ce = r.pick(child.size()), cs = subtree_start(child, ce);
                const std::size_t de = r.pick(donor.size()), ds = subtree_start(donor, de);
                Program out(child.begin(), child.begin() + static_cast<std::ptrdiff_t>(cs));
                out.insert(out.end(), donor.begin() + static_cast<std::ptrdiff_t>(ds), donor.begin() + static_cast<std::ptrdiff_t>(de + 1));
                out.insert(out.end(), child.begin() + static_cast<std::ptrdiff_t>(ce + 1), child.end());
                if (out.size() <= prm.max_size) child = std::move(out);
            } else if (u < prm.p_crossover + prm.p_mutation) {
                const std::size_t e = r.pick(child.size());
                Node& nd = child[e];
                if (nd.op == Op::Const && r.u() < 0.5) {
                    nd.c *= 1 + (r.u() - 0.5);  // nudge a constant
                } else {  // replace a subtree by a fresh small one
                    const std::size_t s = subtree_start(child, e);
                    Program fresh;
                    detail::grow(fresh, r, F, 2, false);
                    Program out(child.begin(), child.begin() + static_cast<std::ptrdiff_t>(s));
                    out.insert(out.end(), fresh.begin(), fresh.end());
                    out.insert(out.end(), child.begin() + static_cast<std::ptrdiff_t>(e + 1), child.end());
                    if (out.size() <= prm.max_size) child = std::move(out);
                }
            }
            next[i].prog = std::move(child);
            d.score(next[i]), keep(next[i]);
        }
        std::swap(pop, next);
    }
    return hof;
}

// Infix text with the given feature names.
inline std::string to_string(const Program& p, const std::vector<std::string>& names) {
    std::vector<std::string> st;
    for (const Node& n : p) {
        switch (n.op) {
            case Op::Var: st.push_back(names[n.var]); break;
            case Op::Const: {
                char b[32];
                std::snprintf(b, sizeof b, "%g", n.c);
                st.emplace_back(b);
                break;
            }
            case Op::Neg: st.back() = "-(" + st.back() + ")"; break;
            case Op::Tanh: st.back() = "tanh(" + st.back() + ")"; break;
            default: {
                const std::string r = st.back();
                st.pop_back();
                static constexpr const char* sym[] = {"", "", " + ", " - ", " * ", " / "};
                st.back() = "(" + st.back() + sym[static_cast<int>(n.op)] + r + ")";
            }
        }
    }
    return st.empty() ? "" : st.back();
}

}  // namespace hft::strategy::symreg
