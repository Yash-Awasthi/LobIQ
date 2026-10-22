#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "data/lobster.hpp"

using namespace hft;
namespace lb = data::lobster;

TEST(Lobster, EachItchMessageBecomesItsLobsterEvents) {
    book::TickBook b;
    std::vector<lb::Event> ev;
    std::vector<std::string> books;
    auto emit = [&](const lb::Event& e) { ev.push_back(e), books.push_back(lb::book_line(b, 2)); };
    lb::Translator<decltype(emit)> tr(b, emit);
    tr(itch::AddOrder{{}, 1, 'B', 300, {}, 100'000, {}});  // bid 10.00 x 300
    tr(itch::AddOrder{{}, 2, 'S', 200, {}, 100'100, {}});  // ask 10.01 x 200
    tr(itch::OrderExecuted{{}, 2, 50, 9});                  // 50 of the ask trade
    tr(itch::OrderCancel{{}, 1, 100});                      // 100 of the bid cancelled
    tr(itch::OrderReplace{{}, 1, 3, 400, 99'900});          // the bid moves to 9.99 x 400
    tr(itch::OrderDelete{{}, 2});
    tr(itch::OrderDelete{{}, 77});                          // unknown order: nothing
    const std::vector<std::array<std::int64_t, 4>> want = {
        {1, 1, 300, 100'000}, {1, 2, 200, 100'100}, {4, 2, 50, 100'100}, {2, 1, 100, 100'000},
        {3, 1, 200, 100'000}, {1, 3, 400, 99'900},  {3, 2, 150, 100'100}};
    ASSERT_EQ(ev.size(), want.size());
    for (std::size_t i = 0; i < ev.size(); ++i) {
        EXPECT_EQ(ev[i].type, want[i][0]) << i;
        EXPECT_EQ(ev[i].id, static_cast<std::uint64_t>(want[i][1])) << i;
        EXPECT_EQ(ev[i].size, static_cast<std::uint64_t>(want[i][2])) << i;
        EXPECT_EQ(ev[i].price, want[i][3]) << i;
    }
    EXPECT_EQ(ev[0].dir, 1);
    EXPECT_EQ(ev[2].dir, -1);  // a sell limit order executed: a buyer-initiated trade
    EXPECT_EQ(books[2], "100100,150,100000,300,9999999999,0,-9999999999,0\n");
    EXPECT_EQ(books[4], "100100,150,-9999999999,0,9999999999,0,-9999999999,0\n");  // between the replace's two halves
    EXPECT_EQ(books[5], "100100,150,99900,400,9999999999,0,-9999999999,0\n");
}

TEST(Lobster, MessageLineKeepsNanoseconds) {
    EXPECT_EQ(lb::message_line(34'713'685'155'243ull, lb::Event{1, 206833312, 100, 118600, -1}),
              "34713.685155243,1,206833312,100,118600,-1\n");  // the example on LOBSTER's data structure page
}
