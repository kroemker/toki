#include "BuildMap.h"

#include <algorithm>
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hackeroot {

namespace {

[[nodiscard]] bool isBlank(const char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

[[nodiscard]] std::string_view skipBlanks(std::string_view text) {
    while (!text.empty() && isBlank(text.front())) {
        text.remove_prefix(1);
    }
    return text;
}

[[nodiscard]] std::optional<std::pair<std::string_view, u32>>
findSymbol(std::string_view line, const std::span<const std::string_view> names) {
    line = skipBlanks(line);
    if (!line.starts_with("0x")) {
        return std::nullopt;
    }
    line.remove_prefix(2);
    u64 address = 0;
    const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), address, 16);
    if (error != std::errc{}) {
        return std::nullopt;
    }
    line = skipBlanks(line.substr(static_cast<usize>(end - line.data())));
    const usize nameEnd =
        std::ranges::find_if(line, [](const char c) { return isBlank(c); }) - line.begin();
    const std::string_view name = line.substr(0, nameEnd);
    if (std::ranges::find(names, name) == names.end()) {
        return std::nullopt;
    }
    return std::pair{name, static_cast<u32>(address)};
}

} // namespace

std::map<std::string, u32, std::less<>>
findMapSymbols(std::string_view text, const std::span<const std::string_view> names) {
    std::map<std::string, u32, std::less<>> symbols;
    while (!text.empty()) {
        const usize lineEnd = std::min(text.find('\n'), text.size());
        if (const auto symbol = findSymbol(text.substr(0, lineEnd), names)) {
            symbols.try_emplace(std::string(symbol->first), symbol->second);
        }
        text.remove_prefix(std::min(lineEnd + 1, text.size()));
    }
    return symbols;
}

std::expected<BuildMap, std::string> readBuildMap(const std::filesystem::path& file,
                                                  const std::span<const std::string_view> names) {
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, error);
    if (error) {
        return std::unexpected(std::format("{}: {}", file.generic_string(), error.message()));
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("{}: cannot be opened", file.generic_string()));
    }
    BuildMap map{.file = file, .written = written};
    std::string line;
    while (map.symbols.size() < names.size() && std::getline(stream, line)) {
        if (const auto symbol = findSymbol(line, names)) {
            map.symbols.try_emplace(std::string(symbol->first), symbol->second);
        }
    }
    return map;
}

} // namespace hackeroot
