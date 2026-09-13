#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace toki::core {

/// Converts a path to UTF-8. std::filesystem::path's own narrow conversions go
/// through the active code page on Windows, which mangles anything outside
/// ASCII; config files, SDL and the decomp tree are all UTF-8.
[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] inline std::filesystem::path fromUtf8(std::string_view text) {
    return std::filesystem::path{
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

} // namespace toki::core
