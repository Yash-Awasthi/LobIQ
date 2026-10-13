#pragma once

// Market-abuse candidates from one symbol's ITCH stream (apps/itch_surveil.cpp). Nasdaq's public feed
// names the participant only on attributed orders (MPID), so these are patterns for a reviewer, not
// findings against anyone:
// - spoof: a large order at the touch, cancelled within seconds with nothing filled, while the
//   other side traded;
// - layer: one participant's (MPID) burst of same-side orders over several levels, cancelled unfilled;
// - stuffing: a 100 ms burst of messages far above the symbol's usual rate, almost no executions.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "book/itch_apply.hpp"
#include "book/tick_book.hpp"
#include "feed/itch.hpp"

namespace hft::surveil {

struct Params {
    double size_multiple = 10;           // order (or layer) size against the typical best-level depth
    std::uint32_t min_shares = 5000;     // and never smaller than this
    std::uint64_t min_opposite = 1000;   // spoof: shares executed on the other side while it showed
    std::uint64_t max_life_ns = 2'000'000'000;
    std::uint64_t burst_ns = 50'000'000;  // layer: adds this close together form one burst
    std::uint32_t min_orders = 4, min_levels = 3;
    std::uint32_t layer_ticks = 10;       // layer orders lie within this many ticks of their best
    std::uint64_t bucket_ns = 100'000'000;
    std::uint32_t min_msgs = 1000;        // stuffing: messages in one bucket, at least
    double rate_multiple = 50;            // and this many times the symbol's median busy bucket
    double max_exec_share = 0.01;         // with executions at most this share of the messages
    double min_cancel_share = 0.4;        // and cancels at least this share: churn, not a rush of new orders
    // 09:35 to 15:50: the opening and closing auctions' order flow is not continuous trading.
    std::uint64_t start_ns = 34'500'000'000'000, end_ns = 57'000'000'000'000;
};

using Mpid = std::array<char, 4>;  // all zero when the order was not attributed

struct Spoof {
    std::uint64_t add_ts, end_ts, ref;
    char side;
    std::uint32_t price, shares;
    double depth_multiple;
    std::uint64_t opposite_exec;  // shares executed against the other side while it rested
    double mid_move_ticks;        // mid change over its life, positive toward the side it was on
    Mpid mpid;
};

struct Layer {
    std::uint64_t start_ts, end_ts;
    char side;
    std::uint32_t orders, levels;
    std::uint64_t shares;
    double depth_multiple;
    std::uint64_t opposite_exec;
    Mpid mpid;
};

struct Stuffing {
    std::uint64_t start_ts, end_ts, msgs, cancels, execs;
    double rate_multiple;  // peak bucket against the median busy bucket
};

class Detector {
   public:
    explicit Detector(Params p = {}) : p_(p), ap_{book_} {}

    void on_message(const std::uint8_t* msg, std::size_t len) {
        ts_ = itch::detail::read_header(msg).timestamp;
        before_ = book_.bbo();
        pre_.self = this;
        itch::dispatch(msg, len, pre_);  // sees the book as the message found it
        itch::dispatch(msg, len, ap_);
        const book::Bbo q = book_.bbo();
        if (q.bid_px && q.ask_px && q.ask_px > q.bid_px) {
            const double d = (q.bid_qty + q.ask_qty) / 2.0;
            depth_ = depth_ > 0 ? 0.999 * depth_ + 0.001 * d : d;
            mid_ = (q.bid_px + q.ask_px) / 2.0;
            if (!tick_) tick_ = q.bid_px >= 10'000 ? 100 : 1;
        }
    }

    // Resolves layers and stuffing; call once after the last message.
    void finish() {
        // Judged on the burst's orders that were cancelled unfilled within max_life: other traders'
        // orders interleave in the same milliseconds on an anonymous feed.
        for (const Burst& b : bursts_)
            if (b.clean >= p_.min_orders && b.prices.size() >= p_.min_levels &&
                b.shares >= std::max<double>(p_.min_shares, p_.size_multiple * b.depth))
                layers_.push_back({b.start_ts, b.end_ts, b.side, b.clean, static_cast<std::uint32_t>(b.prices.size()),
                                   b.shares, b.depth > 0 ? b.shares / b.depth : 0, b.opp_end - b.opp_start, b.mpid});
        std::vector<std::uint32_t> busy;
        for (const auto& [k, c] : buckets_) busy.push_back(c.msgs);
        if (busy.empty()) return;
        std::nth_element(busy.begin(), busy.begin() + busy.size() / 2, busy.end());
        const double median = std::max<std::uint32_t>(busy[busy.size() / 2], 1);
        Stuffing* open = nullptr;
        std::uint64_t last = 0;
        for (const auto& [k, c] : buckets_) {
            const bool hot = c.msgs >= p_.min_msgs && c.msgs >= p_.rate_multiple * median &&
                             c.execs <= p_.max_exec_share * c.msgs && c.cancels >= p_.min_cancel_share * c.msgs;
            if (!hot) continue;
            const std::uint64_t t0 = k * p_.bucket_ns;
            if (open && k == last + 1) {
                open->end_ts = t0 + p_.bucket_ns, open->msgs += c.msgs, open->cancels += c.cancels, open->execs += c.execs;
                open->rate_multiple = std::max(open->rate_multiple, c.msgs / median);
            } else {
                stuffing_.push_back({t0, t0 + p_.bucket_ns, c.msgs, c.cancels, c.execs, c.msgs / median});
                open = &stuffing_.back();
            }
            last = k;
        }
    }

    const std::vector<Spoof>& spoofs() const { return spoofs_; }
    const std::vector<Layer>& layers() const { return layers_; }
    const std::vector<Stuffing>& stuffing() const { return stuffing_; }

   private:
    struct Order {
        std::uint64_t add_ts;
        std::uint32_t price, shares, left;
        char side;
        bool touch, filled;
        double depth, mid;
        std::uint64_t opp_at_add;
        std::int32_t burst;  // index into bursts_, -1 when not part of one
        Mpid mpid;
    };
    struct Burst {
        std::uint64_t start_ts, last_add, end_ts = 0;
        char side;
        Mpid mpid;
        std::uint32_t orders = 0, clean = 0;  // all orders, and those cancelled unfilled within max_life
        std::uint64_t shares = 0;             // shares and distinct prices of the clean ones
        double depth;
        std::vector<std::uint32_t> prices;
        std::uint64_t opp_start, opp_end = 0;
    };
    struct Counts {
        std::uint32_t msgs = 0, cancels = 0, execs = 0;
    };

    struct Pre {
        Detector* self;
        void operator()(const itch::AddOrder& m) {
            self->count(0);
            self->add(m.ref, m.side, m.shares, m.price, m.mpid);
        }
        void operator()(const itch::OrderExecuted& m) {
            self->count(1);
            self->exec(m.ref, m.shares);
        }
        void operator()(const itch::OrderExecutedPrice& m) {
            self->count(1);
            self->exec(m.ref, m.shares);
        }
        void operator()(const itch::OrderCancel& m) {
            self->count(2);
            self->reduce(m.ref, m.cancelled);
        }
        void operator()(const itch::OrderDelete& m) {
            self->count(2);
            self->end(m.ref);
        }
        void operator()(const itch::OrderReplace& m) {
            self->count(2);
            const auto it = self->live_.find(m.orig_ref);
            const char side = it != self->live_.end() ? it->second.side : 0;
            const Mpid mpid = it != self->live_.end() ? it->second.mpid : Mpid{};
            self->end(m.orig_ref);
            if (side) self->add(m.new_ref, side, m.shares, m.price, mpid.data());
        }
        template <class T>
        void operator()(const T&) {}
    };

    bool in_window() const { return ts_ >= p_.start_ns && ts_ < p_.end_ns; }
    void count(int kind) {  // 0 add, 1 execution, 2 cancel, delete or replace
        if (!in_window()) return;
        Counts& c = buckets_[ts_ / p_.bucket_ns];
        ++c.msgs, c.execs += kind == 1, c.cancels += kind == 2;
    }

    void add(std::uint64_t ref, char side, std::uint32_t shares, std::uint32_t price, const char* mpid) {
        if (!in_window()) return;
        const bool buy = side == 'B';
        const std::uint32_t best = buy ? before_.bid_px : before_.ask_px;
        const std::uint32_t other = buy ? before_.ask_px : before_.bid_px;
        const bool crosses = other && (buy ? price >= other : price <= other);
        const bool touch = !crosses && (!best || (buy ? price >= best : price <= best));
        Order o{ts_, price, shares, shares, side, touch, false, depth_, mid_, opp(side), -1, {}};
        std::memcpy(o.mpid.data(), mpid, 4);
        // Layer bursts: orders near their best, grouped by side and participant.
        const std::uint32_t tick = tick_ ? tick_ : 1;
        const bool near = !crosses && (!best || (buy ? price + p_.layer_ticks * tick >= best : price <= best + p_.layer_ticks * tick));
        if (near && o.mpid[0]) {  // a layer is one participant's: only attributed orders say whose
            const std::string key = std::string(1, side) + std::string(o.mpid.data(), 4);
            auto& cur = open_burst_.try_emplace(key, -1).first->second;
            if (cur < 0 || ts_ - bursts_[static_cast<std::size_t>(cur)].start_ts > p_.burst_ns) {
                bursts_.push_back({ts_, ts_, 0, side, o.mpid, 0, 0, 0, depth_, {}, opp(side), 0});
                cur = static_cast<std::int32_t>(bursts_.size() - 1);
            }
            Burst& b = bursts_[static_cast<std::size_t>(cur)];
            b.last_add = ts_, ++b.orders;
            o.burst = cur;
        }
        live_[ref] = o;
    }
    void exec(std::uint64_t ref, std::uint32_t shares) {
        if (const auto r = book_.order(ref)) cum_exec_[r->side == book::Side::Buy ? 0 : 1] += shares;
        const auto it = live_.find(ref);
        if (it == live_.end()) return;
        Order& o = it->second;
        o.filled = true;
        o.left = o.left > shares ? o.left - shares : 0;
        if (!o.left) end(ref);
    }
    void reduce(std::uint64_t ref, std::uint32_t shares) {
        const auto it = live_.find(ref);
        if (it == live_.end()) return;
        it->second.left = it->second.left > shares ? it->second.left - shares : 0;
        if (!it->second.left) end(ref);
    }
    // Shares executed so far against resting orders on the side opposite `side`.
    std::uint64_t opp(char side) const { return cum_exec_[side == 'B' ? 1 : 0]; }

    void end(std::uint64_t ref) {
        const auto it = live_.find(ref);
        if (it == live_.end()) return;
        const Order o = it->second;
        live_.erase(it);
        const bool quick = ts_ - o.add_ts <= p_.max_life_ns;
        if (o.burst >= 0) {
            Burst& b = bursts_[static_cast<std::size_t>(o.burst)];
            if (!o.filled && quick) {  // counts toward the layer: its price, shares and last cancel
                ++b.clean, b.shares += o.shares;
                if (std::find(b.prices.begin(), b.prices.end(), o.price) == b.prices.end()) b.prices.push_back(o.price);
                b.end_ts = std::max(b.end_ts, ts_);
                b.opp_end = opp(o.side);
            }
        }
        if (o.filled || !quick || !o.touch || o.shares < std::max<double>(p_.min_shares, p_.size_multiple * o.depth) ||
            opp(o.side) - o.opp_at_add < p_.min_opposite)
            return;
        const double move = o.mid > 0 && mid_ > 0 ? (mid_ - o.mid) / (tick_ ? tick_ : 1) : 0;
        spoofs_.push_back({o.add_ts, ts_, ref, o.side, o.price, o.shares, o.depth > 0 ? o.shares / o.depth : 0,
                           opp(o.side) - o.opp_at_add, o.side == 'B' ? move : -move, o.mpid});
    }

    Params p_;
    book::TickBook book_;
    book::ItchApply<book::TickBook> ap_;
    Pre pre_{nullptr};
    std::uint64_t ts_ = 0;
    book::Bbo before_{};
    double depth_ = 0, mid_ = 0;
    std::uint32_t tick_ = 0;
    std::array<std::uint64_t, 2> cum_exec_{};  // shares executed against resting bids [0] and asks [1]
    std::unordered_map<std::uint64_t, Order> live_;
    std::vector<Burst> bursts_;
    std::map<std::string, std::int32_t> open_burst_;
    std::map<std::uint64_t, Counts> buckets_;
    std::vector<Spoof> spoofs_;
    std::vector<Layer> layers_;
    std::vector<Stuffing> stuffing_;
};

}  // namespace hft::surveil
