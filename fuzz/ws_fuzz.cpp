// WebSocket frames from the network are untrusted (SECURITY SEC1). However the bytes are split
// into reads, the parser must give the same messages, or fail the same way, as with one read.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "net/ws_frames.hpp"

namespace {

// Messages as "op:data", then "!" if the parser threw.
std::vector<std::string> run(const char* p, std::size_t n, std::size_t chunk) {
    std::vector<std::string> out;
    hft::net::WsParser w;
    try {
        for (std::size_t i = 0; i < n || i == 0; i += chunk) {
            const std::size_t k = i < n ? std::min(chunk, n - i) : 0;
            std::memcpy(w.reserve(k), p + i, k), w.commit(k);
            hft::net::WsView v;
            while (w.next_view(v)) out.push_back(std::to_string(v.op) + ":" + std::string(v.data));
            if (k == 0) break;
        }
    } catch (const std::runtime_error&) {
        out.push_back("!");
    }
    return out;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1) return 0;
    const std::size_t chunk = 1 + data[0] % 64;
    const char* p = reinterpret_cast<const char*>(data + 1);
    if (run(p, size - 1, chunk) != run(p, size - 1, size)) std::abort();
    return 0;
}
