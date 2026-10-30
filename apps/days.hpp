#pragma once

// Train and test days for the walk-forward research apps: store lists, the day of a store, and the
// symbol universe (named symbols, or the most active of the first day that trade on every day).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "data/store.hpp"

namespace hft::app {

inline std::vector<std::string> split_list(const std::string& v) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= v.size()) {
        const std::size_t j = std::min(v.find(',', i), v.size());
        if (j > i) out.push_back(v.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

inline std::vector<std::filesystem::path> paths(const std::string& v) {
    std::vector<std::filesystem::path> out;
    for (const std::string& s : split_list(v)) out.emplace_back(s);
    return out;
}

// store-S120825-v50 -> 2025-12-08; any other name as it is.
inline std::string day_of(const std::filesystem::path& p) {
    const std::string s = p.filename().string();
    const auto at = s.find('S');
    return at == std::string::npos || s.size() < at + 7 ? s : "20" + s.substr(at + 5, 2) + "-" + s.substr(at + 1, 2) + "-" + s.substr(at + 3, 2);
}

struct Universe {
    std::vector<std::string> names;
    std::vector<std::map<std::string, std::uint16_t>> locate;  // per day
    std::string error;
};

// `symbols` is a number N (the N most active of days[0] present on every day) or a comma list.
inline Universe universe(const std::vector<std::filesystem::path>& days, const std::string& symbols) {
    Universe u;
    for (const auto& d : days) {
        u.locate.push_back(data::read_symbols(d));
        if (u.locate.back().empty()) return u.error = "no symbols.tsv in " + d.string() + " (written by ingest)", u;
    }
    const bool count = !symbols.empty() && std::all_of(symbols.begin(), symbols.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (!count) {
        u.names = split_list(symbols);
        for (const auto& n : u.names)
            for (std::size_t d = 0; d < days.size(); ++d)
                if (!u.locate[d].contains(n)) return u.error = "symbol " + n + " not in " + days[d].string(), u;
        if (u.names.empty()) u.error = "no symbols";
        return u;
    }
    std::vector<std::pair<std::uint64_t, std::string>> active;
    for (const auto& [name, loc] : u.locate[0]) {
        bool everywhere = true;
        for (std::size_t d = 1; d < days.size(); ++d) everywhere &= u.locate[d].contains(name);
        if (!everywhere) continue;
        data::SymbolReader rd(days[0], loc);
        std::uint64_t n = 0;
        for (const auto& e : rd.index()) n += e.n_msgs;
        active.emplace_back(n, name);
    }
    std::sort(active.rbegin(), active.rend());
    active.resize(std::min<std::size_t>(active.size(), std::strtoul(symbols.c_str(), nullptr, 10)));
    for (const auto& a : active) u.names.push_back(a.second);
    if (u.names.empty()) u.error = "no symbol trades on every day";
    return u;
}

}  // namespace hft::app
