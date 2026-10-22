#pragma once

// ITCH 5.0 to LOBSTER (lobsterdata.com, "Data structure"): the message file's events and the order
// book after each, as the deep-learning order book benchmarks (LOBCAST, LOBFrame, DeepMarket) read
// them. Event types: 1 submission, 2 partial cancellation, 3 deletion, 4 visible execution,
// 5 hidden execution, 6 cross trade, 7 trading halt (price -1 halted, 0 quoting, 1 trading).
// Direction: 1 buy limit order, -1 sell limit order. Prices are dollars times 10000, as in ITCH.

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "book/tick_book.hpp"
#include "feed/itch.hpp"

namespace hft::data::lobster {

struct Event {
    int type;
    std::uint64_t id;
    std::uint64_t size;
    std::int64_t price;
    int dir;
};

inline constexpr std::int64_t kNoAsk = 9'999'999'999, kNoBid = -9'999'999'999;

// Applies ITCH messages to a book and reports each as LOBSTER events; `emit(event)` runs after the
// book reflects that event, so a caller can snapshot the book there. A replace is a deletion and a
// submission, each with its own snapshot, as LOBSTER records it.
template <class Emit>
class Translator {
   public:
    Translator(book::TickBook& b, Emit emit) : book_(b), emit_(emit) {}

    void operator()(const itch::AddOrder& m) {
        const int dir = m.side == 'B' ? 1 : -1;
        if (!book_.add(m.ref, dir > 0 ? book::Side::Buy : book::Side::Sell, m.shares, m.price, seq)) return;
        orders_[m.ref] = {m.price, dir, m.shares};
        emit_(Event{1, m.ref, m.shares, m.price, dir});
    }
    void operator()(const itch::OrderExecuted& m) { reduce(m.ref, m.shares, 4, true); }
    void operator()(const itch::OrderExecutedPrice& m) { reduce(m.ref, m.shares, 4, true); }
    void operator()(const itch::OrderCancel& m) { reduce(m.ref, m.cancelled, 2, false); }
    void operator()(const itch::OrderDelete& m) {
        const auto it = orders_.find(m.ref);
        if (it == orders_.end() || !book_.erase(m.ref)) return;
        const Order o = it->second;
        orders_.erase(it);
        emit_(Event{3, m.ref, o.left, o.price, o.dir});
    }
    void operator()(const itch::OrderReplace& m) {
        const auto it = orders_.find(m.orig_ref);
        if (it == orders_.end() || !book_.erase(m.orig_ref)) return;
        const Order o = it->second;
        orders_.erase(it);
        emit_(Event{3, m.orig_ref, o.left, o.price, o.dir});
        if (!book_.add(m.new_ref, o.dir > 0 ? book::Side::Buy : book::Side::Sell, m.shares, m.price, seq)) return;
        orders_[m.new_ref] = {m.price, o.dir, m.shares};
        emit_(Event{1, m.new_ref, m.shares, m.price, o.dir});
    }
    void operator()(const itch::Trade& m) { emit_(Event{5, m.ref, m.shares, m.price, m.side == 'B' ? 1 : -1}); }
    void operator()(const itch::CrossTrade& m) { emit_(Event{6, 0, m.shares, m.price, -1}); }
    void operator()(const itch::TradingAction& m) {
        emit_(Event{7, 0, 0, m.state == 'T' ? 1 : m.state == 'Q' ? 0 : -1, -1});
    }
    template <class T>
    void operator()(const T&) {}

    std::uint64_t seq = 0;  // feed sequence of the message being applied

   private:
    struct Order {
        std::uint32_t price;
        int dir;
        std::uint64_t left;
    };
    void reduce(std::uint64_t ref, std::uint64_t shares, int type, bool exec) {
        const auto it = orders_.find(ref);
        if (it == orders_.end() || !(exec ? book_.execute(ref, static_cast<std::uint32_t>(shares)) : book_.cancel(ref, static_cast<std::uint32_t>(shares))))
            return;
        Order& o = it->second;
        const Order seen = o;
        o.left = shares >= o.left ? 0 : o.left - shares;
        if (!o.left) orders_.erase(it);
        emit_(Event{type, ref, shares, seen.price, seen.dir});
    }

    book::TickBook& book_;
    Emit emit_;
    std::unordered_map<std::uint64_t, Order> orders_;
};

// One message-file line: "seconds.nanoseconds,type,id,size,price,dir".
inline std::string message_line(std::uint64_t ts, const Event& e) {
    char b[128];
    std::snprintf(b, sizeof b, "%llu.%09llu,%d,%llu,%llu,%lld,%d\n", static_cast<unsigned long long>(ts / 1'000'000'000),
                  static_cast<unsigned long long>(ts % 1'000'000'000), e.type, static_cast<unsigned long long>(e.id),
                  static_cast<unsigned long long>(e.size), static_cast<long long>(e.price), e.dir);
    return b;
}

// One order-book-file line for `levels` levels: ask price, ask size, bid price, bid size, ...; an
// empty level is the dummy price with size 0.
inline std::string book_line(const book::TickBook& b, int levels) {
    std::vector<std::int64_t> v(static_cast<std::size_t>(4 * levels));
    auto side = [&](book::Side s, int off, std::int64_t none) {
        auto l = b.best_level(s);
        for (int m = 0; m < levels; ++m) {
            v[static_cast<std::size_t>(4 * m + off)] = l ? static_cast<std::int64_t>(l->price) : none;
            v[static_cast<std::size_t>(4 * m + off + 1)] = l ? static_cast<std::int64_t>(l->qty) : 0;
            if (l) l = b.next_level_info(s, l->price);
        }
    };
    side(book::Side::Sell, 0, kNoAsk);
    side(book::Side::Buy, 2, kNoBid);
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + std::to_string(v[i]);
    return out + "\n";
}

}  // namespace hft::data::lobster
