#pragma once

// Strategies by name for itch_backtest. A strategy is any type with `void decide(const View&,
// Desired&)`; HFT_STRATEGY registers a factory for it from a .cpp under strategies/ (docs/STRATEGIES.md).

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "backtest/backtest.hpp"

namespace hft::backtest {

// key=value arguments from the command line; `arg` is the text after "name:" ("dp:policy.bin").
struct Params {
    std::map<std::string, double> kv;
    std::string arg;
    double get(const std::string& k, double def) const {
        const auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
};

// Everything one run needs besides the strategy.
struct RunSpec {
    Config config;
    std::string store;
    std::uint16_t target = 0;
    std::vector<std::uint16_t> index;
    std::uint32_t hysteresis = 0;
};

struct StrategyEntry {
    std::string name, help;
    bool takes_arg = false;  // registered as "name:" and run as "name:<arg>"
    std::function<Summary(const RunSpec&, const Params&)> run;
};

inline std::vector<StrategyEntry>& strategies() {
    static std::vector<StrategyEntry> all;
    return all;
}

inline const StrategyEntry* find_strategy(const std::string& spec) {
    for (const StrategyEntry& e : strategies())
        if (e.takes_arg ? spec.starts_with(e.name + ":") : spec == e.name) return &e;
    return nullptr;
}

template <class S>
Summary run_strategy(const RunSpec& r, S& s) {
    Backtest<S> bt(r.config, s);
    bt.set_hysteresis(r.hysteresis);
    return bt.run(r.store, r.target, r.index);
}

// `make(const Params&, const RunSpec&)` returns the strategy, configured from the parameters.
struct RegisterStrategy {
    template <class Make>
    RegisterStrategy(std::string name, std::string help, Make make) {
        const bool arg = !name.empty() && name.back() == ':';
        if (arg) name.pop_back();
        strategies().push_back({std::move(name), std::move(help), arg, [make](const RunSpec& r, const Params& p) {
                                    auto s = make(p, r);
                                    return run_strategy(r, s);
                                }});
    }
};

}  // namespace hft::backtest

#define HFT_STRATEGY_CAT2(a, b) a##b
#define HFT_STRATEGY_CAT(a, b) HFT_STRATEGY_CAT2(a, b)
// HFT_STRATEGY("name", "one-line help", [](const Params& p, const RunSpec& r) { return MyStrategy{...}; });
#define HFT_STRATEGY(name, help, ...) \
    static const ::hft::backtest::RegisterStrategy HFT_STRATEGY_CAT(hft_strategy_, __COUNTER__)(name, help, __VA_ARGS__)
