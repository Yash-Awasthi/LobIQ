#pragma once

// Execution quality in the spirit of SEC Rule 605, from one symbol's ITCH stream (apps/itch_bestex).
// Displayed executions are signed by the resting order's side (the taker took the other side);
// effective half-spread = realized spread + price impact against the mid 5 s later. Hidden executions
// (P) inside the quote count as price improvement: how much better than the displayed quote the taker
// got. Nasdaq sends every P as a buy, so a P is signed by its price against the mid.

#include <array>
#include <cstdint>
#include <deque>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "feed/itch.hpp"

namespace hft::surveil {

struct ExecBucket {
    double execs = 0, shares = 0;
    double eff = 0, real = 0, impact = 0;      // dollars per share, summed weighted by shares
    double hidden_shares = 0, improved = 0;    // hidden executions inside the quote; improvement, dollars
    void add(const ExecBucket& o) {
        execs += o.execs, shares += o.shares, eff += o.eff, real += o.real, impact += o.impact;
        hidden_shares += o.hidden_shares, improved += o.improved;
    }
};

class BestEx {
   public:
    static constexpr std::size_t kSizes = 4;  // shares per execution: 1-100, 101-500, 501-2,000, more
    static std::size_t size_bucket(std::uint32_t sh) { return sh <= 100 ? 0 : sh <= 500 ? 1 : sh <= 2000 ? 2 : 3; }

    BestEx() : ap_{book_} {}

    void on_message(const std::uint8_t* msg, std::size_t len) {
        ts_ = itch::detail::read_header(msg).timestamp;
        const book::Bbo q = book_.bbo();
        quoted_ = in_window() && q.bid_px && q.ask_px > q.bid_px;
        if (quoted_) bid_ = q.bid_px, ask_ = q.ask_px;
        // Markouts resolve against the mid in force 5 s after each trade.
        while (!pending_.empty() && pending_.front().ts + kMarkout <= ts_ && mid_ > 0) {
            const Pending& p = pending_.front();
            ExecBucket& b = buckets_[size_bucket(p.shares)];
            const double w = static_cast<double>(p.shares) * p.dir / 1e4;  // signed; shares * dir alone is unsigned
            b.real += w * (p.px - mid_);
            b.impact += w * (mid_ - p.mid);
            pending_.pop_front();
        }
        pre_.self = this;
        itch::dispatch(msg, len, pre_);
        itch::dispatch(msg, len, ap_);
        const book::Bbo a = book_.bbo();
        if (a.bid_px && a.ask_px > a.bid_px) mid_ = (a.bid_px + a.ask_px) / 2.0;
    }

    const std::array<ExecBucket, kSizes>& buckets() const { return buckets_; }

   private:
    static constexpr std::uint64_t kMarkout = 5'000'000'000;
    struct Pending {
        std::uint64_t ts;
        double px, mid;
        int dir;
        std::uint32_t shares;
    };
    struct Pre {
        BestEx* self;
        void operator()(const itch::OrderExecuted& m) { self->displayed(m.ref, m.shares, 0); }
        void operator()(const itch::OrderExecutedPrice& m) {
            if (m.printable == 'Y') self->displayed(m.ref, m.shares, m.price);
        }
        void operator()(const itch::Trade& m) { self->hidden(m.shares, m.price); }
        template <class T>
        void operator()(const T&) {}
    };

    bool in_window() const { return ts_ >= kStart && ts_ < kEnd; }

    void displayed(std::uint64_t ref, std::uint32_t shares, std::uint32_t price) {
        if (!quoted_) return;
        const auto o = book_.order(ref);
        if (!o) return;
        const double px = price ? price : o->price, mid = (bid_ + ask_) / 2.0;
        const int dir = o->side == book::Side::Sell ? 1 : -1;  // +1: the taker bought
        ExecBucket& b = buckets_[size_bucket(shares)];
        b.execs += 1, b.shares += shares, b.eff += static_cast<double>(shares) * dir * (px - mid) / 1e4;
        pending_.push_back({ts_, px, mid, dir, shares});
    }
    void hidden(std::uint32_t shares, std::uint32_t price) {
        if (!quoted_ || price <= bid_ || price >= ask_) return;  // only executions strictly inside the quote
        const double mid = (bid_ + ask_) / 2.0;
        const int dir = price > mid ? 1 : price < mid ? -1 : 0;
        // A buyer would have paid the ask, a seller received the bid; at the mid, half the spread either way.
        const double better = dir > 0 ? ask_ - price : dir < 0 ? price - bid_ : (ask_ - bid_) / 2.0;
        ExecBucket& b = buckets_[size_bucket(shares)];
        b.execs += 1, b.shares += shares, b.hidden_shares += shares, b.improved += shares * better / 1e4;
        if (dir) {
            b.eff += static_cast<double>(shares) * dir * (price - mid) / 1e4;
            pending_.push_back({ts_, static_cast<double>(price), mid, dir, shares});
        }
    }

    static constexpr std::uint64_t kStart = 34'500'000'000'000, kEnd = 57'000'000'000'000;  // 09:35-15:50
    book::TickBook book_;
    book::ItchApply<book::TickBook> ap_;
    Pre pre_{nullptr};
    std::uint64_t ts_ = 0;
    bool quoted_ = false;
    double bid_ = 0, ask_ = 0, mid_ = 0;
    std::deque<Pending> pending_;
    std::array<ExecBucket, kSizes> buckets_{};
};

}  // namespace hft::surveil
