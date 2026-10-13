// Corrupted checkpoint images into TickBook::load: it must reject them or leave a book that every
// operation can use without reading out of bounds.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "book/tick_book.hpp"

using hft::book::Side;
using hft::book::TickBook;

namespace {

std::vector<std::uint8_t> image() {
    TickBook b;
    std::uint64_t ref = 1;
    for (std::uint32_t i = 0; i < 40; ++i) {
        b.add(ref++, Side::Buy, 100 + i, 10'0000 - 100 * (i % 7), i);
        b.add(ref++, Side::Sell, 100 + i, 10'0100 + 100 * (i % 5), i);
    }
    b.add(ref++, Side::Buy, 10, 1'0000, 99);   // far below the window: radix
    b.add(ref++, Side::Sell, 10, 10'0150, 99);  // off the grid: overflow array
    for (std::uint64_t r = 3; r < 30; r += 4) b.erase(r);
    std::vector<std::uint8_t> out;
    b.save(out);
    return out;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* d, std::size_t n) {
    static const std::vector<std::uint8_t> base = image();
    std::vector<std::uint8_t> img = base;
    // Each 3-byte group: a position (two bytes, scaled to the image) and a byte to XOR in.
    for (std::size_t i = 0; i + 3 <= n; i += 3) {
        const std::size_t at = (std::size_t{d[i]} << 8 | d[i + 1]) * img.size() / 65536;
        img[at] ^= d[i + 2];
    }
    if (n % 3 == 1) img.resize(img.size() - d[n - 1] % 16);
    TickBook b;
    if (!b.load(img.data(), img.size())) return 0;
    (void)b.bbo();
    (void)b.check();
    (void)b.resting_shares();
    for (std::uint64_t r = 1; r < 90; ++r) {
        (void)b.order(r);
        (void)b.queue_ahead(r);
        if (r % 3 == 0) b.execute(r, 1);
        if (r % 5 == 0) b.erase(r);
    }
    b.add(1000, Side::Buy, 5, 10'0000, 1);
    b.add(1001, Side::Sell, 5, 20'0000, 1);
    (void)b.next_level(Side::Buy, 10'0000);
    return 0;
}
