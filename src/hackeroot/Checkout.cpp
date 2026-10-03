#include "Checkout.h"

#include "base/Diagnostic.h"

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

namespace hackeroot {

namespace {

constexpr std::array Markers = {std::string_view("Makefile"), std::string_view("include/config.h"),
                                std::string_view("include/tables/scene_table.h")};

[[nodiscard]] std::string findMissingMarkers(const std::filesystem::path& directory) {
    std::string missing;
    for (const std::string_view marker : Markers) {
        std::error_code failure;
        if (!std::filesystem::is_regular_file(directory / marker, failure)) {
            missing += missing.empty() ? "" : ", ";
            missing += marker;
        }
    }
    return missing;
}

} // namespace

std::expected<std::filesystem::path, base::Diagnostic>
findCheckoutRoot(const std::filesystem::path& path) {
    std::error_code failure;
    if (!std::filesystem::is_directory(path, failure)) {
        return std::unexpected(
            base::makeError(base::SourceLocation{.file = path}, "the folder cannot be read"));
    }

    const std::filesystem::path start = path.lexically_normal();
    for (std::filesystem::path candidate = start;; candidate = candidate.parent_path()) {
        if (findMissingMarkers(candidate).empty()) {
            return candidate;
        }
        if (candidate == candidate.parent_path() || candidate.parent_path().empty()) {
            break;
        }
    }
    return std::unexpected(base::makeError(
        base::SourceLocation{.file = path},
        std::format("not inside a HackerOoT checkout: it has no {}", findMissingMarkers(start))));
}

} // namespace hackeroot
