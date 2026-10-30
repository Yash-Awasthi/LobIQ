#include <cstddef>
#include <cstdint>

#include "feed/itch.hpp"

namespace {

struct Sink {
    template <class T>
    void operator()(const T&) {
        ++count;
    }
    std::size_t count = 0;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Sink sink;
    lobiq::itch::dispatch(data, size, sink);

    lobiq::itch::Frame frame{};
    std::size_t off = 0;
    while (const std::size_t used = lobiq::itch::next_frame(data + off, size - off, frame)) {
        off += used;
        lobiq::itch::dispatch(frame.data, frame.size, sink);
    }
    return 0;
}
