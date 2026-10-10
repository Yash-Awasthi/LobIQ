// Feed-thread cost per WebSocket message: 600-byte text frames (recorded median ~500 bytes)
// in 16 kB reads, as TLS records arrive.

#include <benchmark/benchmark.h>

#include <cstring>
#include <string>
#include <vector>

#include "net/ws_frames.hpp"

namespace {

std::string burst() {
    const std::string frame = std::string("\x81\x7e\x02\x58", 4) + std::string(600, 'm');
    std::string b;
    for (int i = 0; i < 64; ++i) b += frame;
    return b;
}

// Before: copy the read into the parser, each message into its own string, then into the ring.
void BM_WsCopyPath(benchmark::State& state) {
    const std::string b = burst();
    std::vector<char> ring(1 << 20);
    hft::net::WsParser p;
    hft::net::WsMessage m;
    char tls[16384];
    std::size_t at = 0, n = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < b.size(); i += sizeof tls) {
            const std::size_t k = std::min(sizeof tls, b.size() - i);
            std::memcpy(tls, b.data() + i, k);  // stands for SSL_read into a stack buffer
            p.push(tls, k);
            while (p.next(m)) {
                if (at + m.data.size() > ring.size()) at = 0;
                std::memcpy(ring.data() + at, m.data.data(), m.data.size()), at += m.data.size(), ++n;
            }
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(n));
}
BENCHMARK(BM_WsCopyPath);

// After: the read lands in the parser's buffer, each message is a view copied once, into the ring.
void BM_WsViewPath(benchmark::State& state) {
    const std::string b = burst();
    std::vector<char> ring(1 << 20);
    hft::net::WsParser p;
    hft::net::WsView v;
    std::size_t at = 0, n = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < b.size(); i += 16384) {
            const std::size_t k = std::min<std::size_t>(16384, b.size() - i);
            std::memcpy(p.reserve(k), b.data() + i, k), p.commit(k);  // stands for SSL_read into the parser
            while (p.next_view(v)) {
                if (at + v.data.size() > ring.size()) at = 0;
                std::memcpy(ring.data() + at, v.data.data(), v.data.size()), at += v.data.size(), ++n;
            }
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(n));
}
BENCHMARK(BM_WsViewPath);

}  // namespace
