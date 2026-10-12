#pragma once

#include "hackeroot/Types.h"

#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace hackeroot {

/// What the build's linker map says of the symbols asked for: the address of
/// each one it lists, and when the build wrote it.
struct BuildMap {
    std::filesystem::path file;
    std::filesystem::file_time_type written;
    std::map<std::string, u32, std::less<>> symbols;
};

/// The addresses GNU ld's map `text` gives the symbols of `names`, each on a
/// line of its own, `0x0000000080167dc0  gZBuffer` or
/// `0x0000000080214de0  _buffersSegmentEnd = .`, as listed first. A name the
/// map does not list is left out.
[[nodiscard]] std::map<std::string, u32, std::less<>>
findMapSymbols(std::string_view text, std::span<const std::string_view> names);

/// The symbols of `names` in the map at `file`, read a line at a time and no
/// further than the last of them, since a map runs to tens of megabytes.
[[nodiscard]] std::expected<BuildMap, std::string>
readBuildMap(const std::filesystem::path& file, std::span<const std::string_view> names);

} // namespace hackeroot
