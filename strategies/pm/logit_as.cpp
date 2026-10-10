// The built-in quotes as a plug-in: logit Avellaneda-Stoikov. Running it must give the same decisions
// as running without --quoter (test PmQuoter.LogitAsPluginMatchesTheBuiltIn).

#include <cmath>

#include "pm/quoter.hpp"

namespace {

struct LogitAs final : hft::pm::Quoter {
    hft::pm::Quote quote(const hft::pm::QuoteInput& in) override {
        const hft::pm::MakerParams& p = in.params;
        const double q = in.inventory / p.size;
        const double risk = p.gamma * in.variance * p.horizon_s;
        const double r = std::log(in.mid / (1 - in.mid)) - q * risk;
        const double d = risk / 2 + std::log1p(p.gamma / p.k) / p.gamma;
        auto sig = [](double x) { return 1 / (1 + std::exp(-x)); };
        return {static_cast<std::int32_t>(std::floor(sig(r - d) * 1e4 / in.tick) * in.tick),
                static_cast<std::int32_t>(std::ceil(sig(r + d) * 1e4 / in.tick) * in.tick)};
    }
};

HFT_QUOTER("logit_as", "Avellaneda-Stoikov in logit units (the default); keys: gamma k horizon_s", [] { return new LogitAs; });

}  // namespace
