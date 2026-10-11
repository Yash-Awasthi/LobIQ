// Runs the Verilated itch_parser on a BinaryFILE stream and checks every message it emits
// against hft::itch::dispatch. Usage: itch_rtl_test [file]; without a file, a random stream.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

#include "Vitch_parser.h"
#include "feed/itch.hpp"

namespace itch = hft::itch;

struct Msg {
    int status = 0;
    std::uint8_t type = 0, side = 0, flag = 0;
    std::uint16_t locate = 0, tracking = 0;
    std::uint64_t ts = 0, ref = 0, new_ref = 0, match = 0, shares = 0, stock = 0;
    std::uint32_t price = 0;
    bool operator==(const Msg&) const = default;
};

static std::uint64_t stock_of(const char (&s)[8]) {
    std::uint64_t v;
    std::memcpy(&v, s, 8);
    return v;
}

struct Expect {
    Msg m;
    void operator()(const itch::Header& h) {
        m.type = static_cast<std::uint8_t>(h.type);
        m.locate = h.locate, m.tracking = h.tracking, m.ts = h.timestamp;
    }
    template <class T>
    void operator()(const T& x) { (*this)(x.h); }
    void operator()(const itch::AddOrder& x) {
        (*this)(x.h);
        m.ref = x.ref, m.side = x.side, m.shares = x.shares, m.stock = stock_of(x.stock), m.price = x.price;
    }
    void operator()(const itch::Trade& x) {
        (*this)(x.h);
        m.ref = x.ref, m.side = x.side, m.shares = x.shares, m.stock = stock_of(x.stock), m.price = x.price;
        m.match = x.match;
    }
    void operator()(const itch::OrderExecuted& x) {
        (*this)(x.h);
        m.ref = x.ref, m.shares = x.shares, m.match = x.match;
    }
    void operator()(const itch::OrderExecutedPrice& x) {
        (*this)(x.h);
        m.ref = x.ref, m.shares = x.shares, m.match = x.match, m.flag = x.printable, m.price = x.price;
    }
    void operator()(const itch::OrderCancel& x) {
        (*this)(x.h);
        m.ref = x.ref, m.shares = x.cancelled;
    }
    void operator()(const itch::OrderDelete& x) {
        (*this)(x.h);
        m.ref = x.ref;
    }
    void operator()(const itch::OrderReplace& x) {
        (*this)(x.h);
        m.ref = x.orig_ref, m.new_ref = x.new_ref, m.shares = x.shares, m.price = x.price;
    }
    void operator()(const itch::CrossTrade& x) {
        (*this)(x.h);
        m.shares = x.shares, m.stock = stock_of(x.stock), m.price = x.price, m.match = x.match;
        m.flag = x.cross_type;
    }
    void operator()(const itch::BrokenTrade& x) {
        (*this)(x.h);
        m.match = x.match;
    }
};

// Nine in ten frames are well formed with random fields; the rest have a random length and type.
static std::vector<std::uint8_t> random_stream(std::size_t frames, std::uint64_t seed) {
    static const char kTypes[] = "SRHYLVWKJhAFECXDUPQBINO";
    std::mt19937_64 rng(seed);
    std::vector<std::uint8_t> s;
    for (std::size_t i = 0; i < frames; ++i) {
        const bool good = rng() % 10 != 0;
        const char t = kTypes[rng() % 23];
        const std::size_t len = good ? itch::length_of(t) : rng() % 4 == 0 ? rng() % 3 : rng() % 300;
        s.push_back(static_cast<std::uint8_t>(len >> 8));
        s.push_back(static_cast<std::uint8_t>(len));
        for (std::size_t j = 0; j < len; ++j) s.push_back(static_cast<std::uint8_t>(rng()));
        if (good) s[s.size() - len] = static_cast<std::uint8_t>(t);
    }
    return s;
}

struct Result {
    std::size_t frames = 0, ok = 0, cycles = 0, stalls = 0, lat_min = ~std::size_t{0}, lat_max = 0;
    bool pass = true;
};

// gaps: random beat sizes and idle cycles; otherwise 8 bytes offered every clock.
static Result run(const std::vector<std::uint8_t>& s, bool gaps, std::uint64_t seed) {
    std::vector<Msg> want;
    std::vector<std::size_t> ends;  // offset one past each frame's last byte
    for (std::size_t at = 0;;) {
        itch::Frame f;
        const std::size_t used = itch::next_frame(s.data() + at, s.size() - at, f);
        if (used == 0) break;
        Expect e;
        const auto st = itch::dispatch(f.data, f.size, e);
        want.push_back(st == itch::Status::Ok ? e.m : Msg{.status = static_cast<int>(st)});
        at += used;
        ends.push_back(at);
    }

    Result r;
    r.frames = want.size();
    std::mt19937_64 rng(seed);
    Vitch_parser top;
    top.rst = 1;
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
    top.rst = 0;

    std::vector<std::size_t> taken(ends.size());
    std::size_t pos = 0, next_end = 0, got = 0, idle = 0;
    while (got < want.size() && idle < 1000) {
        top.clk = 0;
        std::size_t n = std::min<std::size_t>(gaps ? rng() % 9 : 8, s.size() - pos);
        top.in_valid = n > 0;
        top.in_bytes = static_cast<std::uint8_t>(n);
        std::uint64_t d = 0;
        std::memcpy(&d, s.data() + pos, n);
        top.in_data = d;
        top.eval();
        if (!top.in_ready) n = 0, r.stalls += top.in_valid;
        top.clk = 1;
        top.eval();
        ++r.cycles;
        pos += n;
        while (next_end < ends.size() && ends[next_end] <= pos) taken[next_end++] = r.cycles;
        if (!top.out_valid) {
            ++idle;
            continue;
        }
        idle = 0;
        Msg m{.status = top.out_status};
        if (m.status == 0) {
            m.type = top.out_type, m.side = top.out_side, m.flag = top.out_flag;
            m.locate = top.out_locate, m.tracking = top.out_tracking, m.ts = top.out_ts;
            m.ref = top.out_ref, m.new_ref = top.out_new_ref, m.match = top.out_match;
            m.shares = top.out_shares, m.stock = top.out_stock, m.price = top.out_price;
            ++r.ok;
        }
        if (!(m == want[got])) {
            std::fprintf(stderr, "frame %zu differs: rtl status %d type %c, expected status %d type %c\n", got,
                         m.status, m.type, want[got].status, want[got].type);
            r.pass = false;
            return r;
        }
        if (m.status == 0) {  // oversized frames are reported before their last byte arrives
            const std::size_t lat = r.cycles - taken[got];
            r.lat_min = std::min(r.lat_min, lat), r.lat_max = std::max(r.lat_max, lat);
        }
        ++got;
    }
    if (got != want.size()) {
        std::fprintf(stderr, "rtl emitted %zu of %zu frames\n", got, want.size());
        r.pass = false;
    }
    return r;
}

int main(int argc, char** argv) {
    std::vector<std::uint8_t> s;
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        s.assign(std::istreambuf_iterator<char>(f), {});
    } else {
        s = random_stream(200'000, 1);
    }
    bool pass = true;
    for (const bool gaps : {true, false}) {
        const Result r = run(s, gaps, 2);
        std::printf("%s: frames %zu ok %zu, %zu bytes in %zu cycles (%.2f B/cycle), stalls %zu, latency %zu-%zu cycles, %s\n",
                    gaps ? "random beats" : "full rate", r.frames, r.ok, s.size(), r.cycles,
                    double(s.size()) / double(r.cycles), r.stalls, r.lat_min, r.lat_max, r.pass ? "match" : "MISMATCH");
        pass = pass && r.pass;
    }
    return pass ? 0 : 1;
}
