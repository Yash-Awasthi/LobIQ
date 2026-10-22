#pragma once

// Markov chain of the top of the book (docs/METHODS.md "Markov chain of the book"). A state is the
// spread in ticks (capped) and the bid's share of the best-level size, in equal buckets: the state
// space of Stoikov's micro-price. Counted are the moves between states and, at each change of the
// mid, its direction from the state just before. The direction counts of the fitting period
// predict the moves of the test period; the hit rate is scored against always guessing the more
// frequent direction.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "book/types.hpp"

namespace hft::strategy {

class MarkovBook {
   public:
    MarkovBook(int buckets, int max_spread_ticks, std::uint32_t tick)
        : buckets_(buckets), spreads_(max_spread_ticks), tick_(tick),
          trans_(static_cast<std::size_t>(states() * states()), 0), up_(states(), 0), down_(states(), 0),
          time_ns_(states(), 0) {}

    int states() const { return buckets_ * spreads_; }
    int buckets() const { return buckets_; }
    int max_spread() const { return spreads_; }

    // -1 for a crossed, locked or one-sided book.
    int state(const book::Bbo& b) const {
        if (!b.bid_px || !b.ask_px || b.ask_px <= b.bid_px || !b.bid_qty || !b.ask_qty) return -1;
        const std::uint32_t ticks = (b.ask_px - b.bid_px + tick_ / 2) / tick_;
        const int s = static_cast<int>(std::min<std::uint32_t>(std::max<std::uint32_t>(ticks, 1), spreads_)) - 1;
        const double share = static_cast<double>(b.bid_qty) / static_cast<double>(b.bid_qty + b.ask_qty);
        const int k = std::min(buckets_ - 1, static_cast<int>(share * buckets_));
        return s * buckets_ + k;
    }

    // After every book event. `fit`: counts update (the fitting period); otherwise moves are scored.
    void observe(const book::Bbo& b, std::uint64_t ts, bool fit) {
        const int s = state(b);
        if (prev_ >= 0 && ts > prev_ts_) time_ns_[prev_] += ts - prev_ts_;
        if (s < 0) {
            prev_ = -1;
            return;
        }
        const std::uint64_t mid2 = static_cast<std::uint64_t>(b.bid_px) + b.ask_px;  // twice the mid
        if (prev_ >= 0) {
            if (s != prev_) ++trans_[static_cast<std::size_t>(prev_ * states() + s)];
            if (mid2 != prev_mid2_) {
                const bool up = mid2 > prev_mid2_;
                if (fit) {
                    (up ? up_ : down_)[prev_]++;
                } else {
                    ++test_moves_, test_up_ += up;
                    const std::uint64_t u = up_[prev_], d = down_[prev_];
                    if (u != d && (u > d) == up) ++test_hits_;  // a tie predicts nothing: a miss
                }
            }
        }
        prev_ = s, prev_mid2_ = mid2, prev_ts_ = ts;
    }

    std::uint64_t transitions(int from, int to) const { return trans_[static_cast<std::size_t>(from * states() + to)]; }
    std::uint64_t ups(int s) const { return up_[s]; }
    std::uint64_t downs(int s) const { return down_[s]; }
    std::uint64_t time_ns(int s) const { return time_ns_[s]; }
    std::uint64_t test_moves() const { return test_moves_; }
    std::uint64_t test_hits() const { return test_hits_; }
    // Hit rate of always guessing the test period's more frequent direction (known only afterwards,
    // so a generous baseline).
    double baseline() const {
        return test_moves_ ? static_cast<double>(std::max(test_up_, test_moves_ - test_up_)) / static_cast<double>(test_moves_) : 0;
    }

   private:
    int buckets_, spreads_;
    std::uint32_t tick_;
    std::vector<std::uint64_t> trans_, up_, down_, time_ns_;
    int prev_ = -1;
    std::uint64_t prev_mid2_ = 0, prev_ts_ = 0;
    std::uint64_t test_moves_ = 0, test_up_ = 0, test_hits_ = 0;
};

}  // namespace hft::strategy
