// An external agent quotes through stdin and stdout, so a reinforcement learning loop in any language
// can trade the real queues of the backtester (rl/lobiq_env.py wraps it as a Gym environment).
// Every step_ms of market time the strategy writes one line,
//   obs {"t":ns,"mid":ticks,"bid":px,"ask":px,"bid_qty":n,"ask_qty":n,"inventory":n,"pnl":usd,"features":[...]}
// and reads one line: "BID_OFFSET ASK_OFFSET SIZE", offsets in ticks behind the best (0 joins it,
// -1 leaves that side empty), or "flat" for no quotes. The quotes stay pegged to the best until the
// next step. Between steps nothing is read, so the replay runs at full speed.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "backtest/registry.hpp"

using namespace hft::backtest;

namespace {

struct Agent {
    std::uint64_t step_ns = 1'000'000'000;
    std::uint32_t tick = 100;
    std::uint64_t next = 0;
    long bid_off = -1, ask_off = -1;
    std::uint32_t size = 0;

    void ask_agent(const View& v, const hft::book::Bbo& b) {
        std::string f;
        for (std::size_t i = 0; i < v.n_features; ++i) {
            char x[32];
            std::snprintf(x, sizeof x, "%s%.6g", i ? "," : "", v.features[i]);
            f += x;
        }
        std::printf("obs {\"t\":%llu,\"mid\":%.2f,\"bid\":%u,\"ask\":%u,\"bid_qty\":%llu,\"ask_qty\":%llu,\"inventory\":%lld,\"pnl\":%.6f,\"features\":[%s]}\n",
                    static_cast<unsigned long long>(v.now), std::isnan(v.mid) ? -1.0 : v.mid, b.bid_px, b.ask_px,
                    static_cast<unsigned long long>(b.bid_qty), static_cast<unsigned long long>(b.ask_qty),
                    static_cast<long long>(v.inventory), static_cast<double>(v.pnl) * 1e-6, f.c_str());
        std::fflush(stdout);
        char line[128];
        if (!std::fgets(line, sizeof line, stdin) || std::strncmp(line, "flat", 4) == 0) {
            bid_off = ask_off = -1;
            return;
        }
        unsigned s = 0;
        if (std::sscanf(line, "%ld %ld %u", &bid_off, &ask_off, &s) != 3) bid_off = ask_off = -1, s = 0;
        size = s;
    }

    void decide(const View& v, Desired& d) {
        const hft::book::Bbo b = v.book.bbo();
        if (v.now >= next) {
            ask_agent(v, b);
            next = v.now + step_ns;
        }
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px || !size) return;
        if (bid_off >= 0 && static_cast<long>(b.bid_px) > bid_off * static_cast<long>(tick)) d.bid_px = b.bid_px - static_cast<std::uint32_t>(bid_off) * tick, d.bid_qty = size;
        if (ask_off >= 0) d.ask_px = b.ask_px + static_cast<std::uint32_t>(ask_off) * tick, d.ask_qty = size;
    }
};

HFT_STRATEGY("agent", "an external agent over stdin and stdout, for reinforcement learning (rl/lobiq_env.py); keys: step_ms",
             [](const Params& p, const RunSpec& r) {
                 Agent a;
                 a.step_ns = static_cast<std::uint64_t>(p.get("step_ms", 1000) * 1e6);
                 a.tick = r.config.exchange.tick;
                 return a;
             });

}  // namespace
