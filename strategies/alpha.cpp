// Serves a linear alpha fitted out of sample by itch_alpha (lobiq alpha ... --model DIR): the
// forecast mid change shifts the reservation price, quotes join the best or step back from it, and
// with `taking` a forecast beyond half the spread plus fees and margin crosses. Run as alpha:<FILE>.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "backtest/registry.hpp"
#include "strategy/features.hpp"

using namespace lobiq::backtest;

namespace {

struct AlphaModel {
    double intercept = 0, half_width = 0;
    std::vector<double> mean, inv_sd, coef;

    void load(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("cannot read " + path);
        std::string magic, key;
        int version = 0;
        if (!(f >> magic >> version) || magic != "LOBIQ-ALPHA" || version != 1) throw std::runtime_error("not an alpha model: " + path);
        std::size_t n = 0;
        double v;
        while (f >> key) {
            if (key == "features") {
                f >> n;
                break;
            }
            f >> v;
            if (key == "intercept") intercept = v;
            if (key == "half_width") half_width = v;
        }
        const auto& names = lobiq::strategy::SymbolFeatures::kNames;
        if (n != names.size()) throw std::runtime_error("alpha model has " + std::to_string(n) + " features, the backtester " + std::to_string(names.size()));
        for (std::size_t j = 0; j < n; ++j) {
            std::string name;
            double m, s, c;
            if (!(f >> name >> m >> s >> c) || name != names[j]) throw std::runtime_error("alpha model feature " + std::to_string(j) + " is not " + names[j]);
            mean.push_back(m), inv_sd.push_back(1 / s), coef.push_back(c);
        }
    }

    double predict(const double* x, std::size_t n) const {
        double y = intercept;
        for (std::size_t j = 0; j < coef.size() && j < n; ++j)
            if (std::isfinite(x[j])) y += coef[j] * std::clamp((x[j] - mean[j]) * inv_sd[j], -5.0, 5.0);
        return y;
    }
};

struct AlphaMaker {
    AlphaModel model;
    std::uint32_t size = 100;
    std::int64_t max_inventory = 500;
    double tick = 100;
    double shade = 1, edge_ticks = 0.5, skew_ticks = 0.5;
    bool taking = false;
    double fee_ticks = 0.3, take_margin = 0.2;

    void decide(const View& v, Desired& d) {
        const lobiq::book::Bbo b = v.book.bbo();
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px || !std::isfinite(v.mid)) return;
        const double pred = model.predict(v.features, v.n_features);
        const double half = (static_cast<double>(b.ask_px) - b.bid_px) / (2 * tick);
        if (taking && std::abs(pred) > half + fee_ticks + take_margin) {
            if (pred > 0 && v.inventory < max_inventory) {
                d.take_side = 1, d.take_qty = size, d.take_limit = b.ask_px;
                return;
            }
            if (pred < 0 && v.inventory > -max_inventory) {
                d.take_side = -1, d.take_qty = size, d.take_limit = b.bid_px;
                return;
            }
        }
        const double r = v.mid + shade * pred - skew_ticks * static_cast<double>(v.inventory) / size;
        const double bid = std::floor(r - edge_ticks + 1e-9) * tick, ask = std::ceil(r + edge_ticks - 1e-9) * tick;
        if (v.inventory < max_inventory && bid > 0) d.bid_px = std::min(b.bid_px, static_cast<std::uint32_t>(bid)), d.bid_qty = size;
        if (v.inventory > -max_inventory)  // compared as doubles: a negative ask must not wrap through the cast
            d.ask_px = static_cast<std::uint32_t>(std::max(static_cast<double>(b.ask_px), ask)), d.ask_qty = size;
    }
};

LOBIQ_STRATEGY("alpha:",
             "linear alpha from itch_alpha: alpha:<SYMBOL.alpha>; keys: size max_inventory shade edge_ticks skew_ticks taking "
             "fee_ticks take_margin",
             [](const Params& p, const RunSpec& r) {
                 AlphaMaker s;
                 s.model.load(p.arg);
                 s.size = static_cast<std::uint32_t>(p.get("size", 100));
                 s.max_inventory = static_cast<std::int64_t>(p.get("max_inventory", 500));
                 s.tick = static_cast<double>(r.config.exchange.tick);
                 s.shade = p.get("shade", 1);
                 s.edge_ticks = p.get("edge_ticks", 0.5);
                 s.skew_ticks = p.get("skew_ticks", 0.5);
                 s.taking = p.get("taking", 0) != 0;
                 s.fee_ticks = p.get("fee_ticks", 0.3);
                 s.take_margin = p.get("take_margin", 0.2);
                 return s;
             });

}  // namespace
