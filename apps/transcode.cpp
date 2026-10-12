// Rewrites a store in another codec, keeping chunk boundaries so checkpoints stay valid.
// Usage: transcode <src-store> <dst-store> [--codec lz4|zstd]

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#include "data/store.hpp"
#include "core/parallel.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    using hft::data::Codec;
    Codec codec = Codec::Lz4;
    std::vector<const char*> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--codec") == 0 && i + 1 < argc) {
            const char* c = argv[++i];
            if (std::strcmp(c, "zstd") == 0)
                codec = Codec::Zstd;
            else if (std::strcmp(c, "lz4") != 0)
                args.clear(), args.resize(3);  // rejected below
        } else {
            args.push_back(argv[i]);
        }
    }
    if (args.size() != 2) {
        std::fprintf(stderr, "usage: transcode <src-store> <dst-store> [--codec lz4|zstd]\n");
        return 2;
    }
    const fs::path src = args[0], dst = args[1];
    fs::create_directories(dst);
    if (!fs::is_empty(dst)) {
        std::fprintf(stderr, "output dir not empty\n");
        return 2;
    }

    std::vector<std::uint16_t> locates;
    for (const auto& e : fs::directory_iterator(src)) {
        const std::string ext = e.path().extension().string();
        if (ext == ".idx")
            locates.push_back(static_cast<std::uint16_t>(std::stoul(e.path().stem().string())));
        else if (ext == ".tsv" || ext == ".sha256" || ext == ".ckp" || ext == ".cki")
            fs::copy_file(e.path(), dst / e.path().filename());
    }
    std::sort(locates.begin(), locates.end());

    std::atomic<std::uint64_t> bytes{0};
    hft::parallel_for(locates.size(), [&](std::size_t i) { bytes += hft::data::transcode_symbol(src, dst, locates[i], codec); });
    std::printf("symbols %zu\ncompressed_bytes %llu\n", locates.size(),
                static_cast<unsigned long long>(bytes.load()));
    return 0;
}
