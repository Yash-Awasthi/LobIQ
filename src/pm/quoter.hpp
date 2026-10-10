#pragma once

// Pluggable quote models for the prediction-market maker (docs/STRATEGIES.md). A quoter proposes
// prices; TokenMaker keeps the safety rules: grid, never crossing, inventory cap, warm-up, re-quote rate.

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pm/book.hpp"

namespace hft::pm {

struct MakerParams {
    double gamma = 0.1, k = 30;
    double horizon_s = 3600;    // time left used in the inventory terms
    double size = 100;          // shares per quote
    double max_inv = 1000;      // shares; the side that would pass it is not quoted
    double min_mid = 0.05, max_mid = 0.95;
    double warmup_s = 30;       // no quotes until the variance has seen this much data
    double requote_s = 0.5;
    double var_tau_s = 60, var_prior = 1e-4;
};

struct QuoteInput {
    const TokenBook& book;
    double mid;        // (best bid + best ask) / 2, as a probability
    double inventory;  // shares, signed
    double variance;   // EWMA of squared logit-mid changes per second
    const MakerParams& params;  // size, max_inv, gamma, k, horizon_s and the rest
    std::int32_t tick;  // price grid, 1e-4 units
    double elapsed_s;   // since this token's first two-sided book
};

// Prices in 1e-4 units; -1 leaves that side unquoted.
struct Quote {
    std::int32_t bid = -1, ask = -1;
};

class Quoter {
   public:
    virtual ~Quoter() = default;
    virtual Quote quote(const QuoteInput& in) = 0;
};

struct QuoterEntry {
    std::string name, help;
    std::function<std::unique_ptr<Quoter>()> make;  // one instance per token
};

inline std::vector<QuoterEntry>& quoters() {
    static std::vector<QuoterEntry> all;
    return all;
}

inline const QuoterEntry* find_quoter(const std::string& name) {
    for (const QuoterEntry& e : quoters())
        if (e.name == name) return &e;
    return nullptr;
}

struct RegisterQuoter {
    template <class Make>
    RegisterQuoter(std::string name, std::string help, Make make) {
        quoters().push_back({std::move(name), std::move(help), [make] { return std::unique_ptr<Quoter>(make()); }});
    }
};

}  // namespace hft::pm

#define HFT_QUOTER_CAT2(a, b) a##b
#define HFT_QUOTER_CAT(a, b) HFT_QUOTER_CAT2(a, b)
// HFT_QUOTER("name", "one-line help", [] { return new MyQuoter; });
#define HFT_QUOTER(name, help, ...) \
    static const ::hft::pm::RegisterQuoter HFT_QUOTER_CAT(hft_quoter_, __COUNTER__)(name, help, __VA_ARGS__)
