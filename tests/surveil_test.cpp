#include "surveil/surveil.hpp"

#include <gtest/gtest.h>

#include "itch_writer.hpp"

#include <string>

using hft::surveil::Detector;

namespace {

constexpr std::uint64_t kTen = 36'000'000'000'000;  // 10:00
constexpr std::uint64_t kMs = 1'000'000;

void feed(Detector& d, ItchWriter& w) { d.on_message(w.b.data(), w.b.size()); }

// A book at $100.00 / $100.01: 100 shares bid, 1,000 offered.
void background(Detector& d, ItchWriter& w) {
    w.ts = kTen;
    feed(d, w.add(1, 'B', 100, 1'000'000));
    feed(d, w.add(2, 'S', 1'000, 1'000'100));
}

}  // namespace

TEST(Surveil, FlagsALargeOrderAtTheTouchCancelledUnfilled) {
    Detector d;
    ItchWriter w;
    background(d, w);
    w.ts = kTen + kMs;
    feed(d, w.add(3, 'B', 10'000, 1'000'000));  // about 18x the depth, joins the best bid
    w.ts = kTen + 100 * kMs;
    feed(d, w.exec(2, 1'000));  // someone buys the whole ask while the big bid shows
    w.ts = kTen + 500 * kMs;
    feed(d, w.del(3));
    d.finish();
    ASSERT_EQ(d.spoofs().size(), 1u);
    const auto& s = d.spoofs()[0];
    EXPECT_EQ(s.ref, 3u);
    EXPECT_EQ(s.side, 'B');
    EXPECT_EQ(s.shares, 10'000u);
    EXPECT_EQ(s.opposite_exec, 1'000u);
    EXPECT_GT(s.depth_multiple, 10);
    EXPECT_EQ(s.end_ts - s.add_ts, 499 * kMs);
}

TEST(Surveil, LeavesFilledSlowAndSmallOrdersAlone) {
    Detector d;
    ItchWriter w;
    background(d, w);
    w.ts = kTen + kMs;
    feed(d, w.add(4, 'B', 10'000, 1'000'000));
    feed(d, w.add(5, 'S', 10'000, 1'000'100));
    feed(d, w.add(6, 'B', 100, 1'000'000));
    w.ts = kTen + 2 * kMs;
    feed(d, w.exec(4, 100));  // partly filled: a real order
    w.ts = kTen + 100 * kMs;
    feed(d, w.del(4));
    feed(d, w.del(6));        // small
    w.ts = kTen + 3'000 * kMs;
    feed(d, w.del(5));        // lived 3 s
    d.finish();
    EXPECT_TRUE(d.spoofs().empty());
}

TEST(Surveil, FlagsALayerOfOrdersAddedAndCancelledTogether) {
    for (const bool one_fills : {false, true}) {
        Detector d;
        ItchWriter w;
        background(d, w);
        for (std::uint32_t i = 0; i < 5; ++i) {
            w.ts = kTen + (1 + 2 * i) * kMs;
            feed(d, w.addf(10 + i, 'B', 2'000, 999'900 - 100 * i, "LAYR"));  // one to five ticks below the best bid
        }
        if (one_fills) {
            w.ts = kTen + 20 * kMs;
            feed(d, w.exec(12, 10));
        }
        for (std::uint32_t i = 0; i < 5; ++i) {
            w.ts = kTen + (400 + i) * kMs;
            feed(d, w.del(10 + i));
        }
        d.finish();
        ASSERT_EQ(d.layers().size(), 1u) << one_fills;  // the filled one drops out; four still qualify
        EXPECT_EQ(d.layers()[0].orders, one_fills ? 4u : 5u);
        EXPECT_EQ(d.layers()[0].levels, one_fills ? 4u : 5u);
        EXPECT_EQ(d.layers()[0].shares, one_fills ? 8'000u : 10'000u);
        EXPECT_EQ(std::string(d.layers()[0].mpid.data(), 4), "LAYR");
        EXPECT_TRUE(d.spoofs().empty());  // below the touch
    }
}

TEST(Surveil, NeedsOneAttributedParticipantForALayer) {
    Detector d;
    ItchWriter w;
    background(d, w);
    for (std::uint32_t i = 0; i < 5; ++i) {  // the same ladder, but anonymous
        w.ts = kTen + (1 + 2 * i) * kMs;
        feed(d, w.add(10 + i, 'B', 2'000, 999'900 - 100 * i));
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        w.ts = kTen + (400 + i) * kMs;
        feed(d, w.del(10 + i));
    }
    d.finish();
    EXPECT_TRUE(d.layers().empty());
}

TEST(Surveil, NeedsEnoughLevelsForALayer) {
    Detector d;
    ItchWriter w;
    background(d, w);
    for (std::uint32_t i = 0; i < 5; ++i) {  // five orders, but on only two price levels
        w.ts = kTen + (1 + i) * kMs;
        feed(d, w.addf(10 + i, 'B', 2'000, 999'900 - 100 * (i % 2), "LAYR"));
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        w.ts = kTen + (300 + i) * kMs;
        feed(d, w.del(10 + i));
    }
    d.finish();
    EXPECT_TRUE(d.layers().empty());
}

TEST(Surveil, FlagsAMessageBurstWithoutExecutions) {
    Detector d;
    ItchWriter w;
    background(d, w);
    std::uint64_t ref = 100;
    for (int i = 0; i < 600; ++i) {  // a quiet minute: one add and delete every 100 ms
        w.ts = kTen + (i + 1) * 100 * kMs;
        feed(d, w.add(ref, 'B', 100, 999'000));
        feed(d, w.del(ref++));
    }
    w.ts = kTen + 70'000 * kMs;
    for (int i = 0; i < 1'000; ++i) {  // then 2,000 messages inside 100 ms
        feed(d, w.add(ref, 'S', 100, 1'001'000));
        feed(d, w.del(ref++));
        w.ts += 50'000;
    }
    d.finish();
    ASSERT_EQ(d.stuffing().size(), 1u);
    EXPECT_EQ(d.stuffing()[0].msgs, 2'000u);
    EXPECT_EQ(d.stuffing()[0].execs, 0u);
    EXPECT_GE(d.stuffing()[0].rate_multiple, 20);
}
