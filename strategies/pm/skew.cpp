// Example quoter (docs/STRATEGIES.md): a fixed half-spread around the mid, shifted against inventory
// one tick per `size` shares held, so a long position is quoted lower and sold sooner.

#include <cmath>

#include "pm/quoter.hpp"

namespace {

struct Skew final : lobiq::pm::Quoter {
    static constexpr int kHalfSpreadTicks = 2;

    lobiq::pm::Quote quote(const lobiq::pm::QuoteInput& in) override {
        const double mid = in.mid * 1e4;  // price units
        const double shift = std::round(in.inventory / in.params.size) * in.tick;
        const double half = kHalfSpreadTicks * in.tick;
        return {static_cast<std::int32_t>(mid - half - shift), static_cast<std::int32_t>(mid + half - shift)};
    }
};

LOBIQ_QUOTER("skew", "example: mid +/- 2 ticks, shifted one tick per quote size of inventory", [] { return new Skew; });

}  // namespace
