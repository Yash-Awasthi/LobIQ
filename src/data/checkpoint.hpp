#pragma once

// Per-symbol book checkpoints written by apps/checkpoint: <locate>.ckp holds zstd-compressed
// TickBook images, <locate>.cki one CheckpointEntry per image.

#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace hft::data {

struct CheckpointEntry {
    std::uint64_t offset;
    std::uint32_t comp_size;
    std::uint32_t raw_size;
    std::uint32_t chunk;  // state before this chunk; == number of chunks for end of day
    std::uint32_t reserved;
    std::uint64_t seq;  // first sequence number of that chunk, or last applied + 1 at end
};
static_assert(sizeof(CheckpointEntry) == 32);

inline std::filesystem::path checkpoint_path(const std::filesystem::path& dir, std::uint16_t loc, const char* ext) {
    char name[16];
    std::snprintf(name, sizeof name, "%05u%s", loc, ext);
    return dir / name;
}

// Entries of a symbol's checkpoints; empty when it has none (symbols under CHECKPOINT_EVERY messages).
inline std::vector<CheckpointEntry> read_checkpoints(const std::filesystem::path& dir, std::uint16_t loc) {
    std::ifstream cki(checkpoint_path(dir, loc, ".cki"), std::ios::binary);
    std::vector<CheckpointEntry> es;
    CheckpointEntry e{};
    while (cki.read(reinterpret_cast<char*>(&e), sizeof e)) es.push_back(e);
    return es;
}

inline std::vector<std::uint8_t> read_state(std::ifstream& ckp, const CheckpointEntry& e) {
    std::vector<std::uint8_t> comp(e.comp_size), raw(e.raw_size);
    ckp.seekg(static_cast<std::streamoff>(e.offset));
    ckp.read(reinterpret_cast<char*>(comp.data()), e.comp_size);
    if (!ckp) throw std::runtime_error("short checkpoint read");
    const std::size_t n = ZSTD_decompress(raw.data(), raw.size(), comp.data(), comp.size());
    if (ZSTD_isError(n) || n != raw.size()) throw std::runtime_error("corrupt checkpoint");
    return raw;
}

}  // namespace hft::data
