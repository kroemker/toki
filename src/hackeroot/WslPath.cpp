#include "WslPath.h"

#include "hackeroot/Types.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hackeroot {

namespace {

constexpr std::array SharePrefixes = {std::string_view("//wsl.localhost/"),
                                      std::string_view("//wsl$/")};

[[nodiscard]] char makeLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return makeLower(a) == makeLower(b); });
}

} // namespace

std::optional<WslPath> findWslPath(const std::filesystem::path& path) {
    std::string text = path.generic_string();
    std::ranges::replace(text, '\\', '/');

    for (const std::string_view prefix : SharePrefixes) {
        if (!startsWithIgnoringCase(text, prefix)) {
            continue;
        }
        const std::string_view rest = std::string_view(text).substr(prefix.size());
        const usize slash = rest.find('/');
        const std::string_view distribution = rest.substr(0, slash);
        if (distribution.empty()) {
            return std::nullopt;
        }
        std::string linuxPath =
            slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
        while (linuxPath.size() > 1 && linuxPath.ends_with('/')) {
            linuxPath.pop_back();
        }
        return WslPath{.distribution = std::string(distribution),
                       .linuxPath = std::move(linuxPath)};
    }
    return std::nullopt;
}

} // namespace hackeroot
