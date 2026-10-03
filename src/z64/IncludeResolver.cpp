#include "IncludeResolver.h"

#include "z64/SourceSet.h"
#include "z64/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr std::string_view GeneratedSuffix = ".inc.c";

[[nodiscard]] std::string_view formatOf(std::string_view stem) {
    const usize dot = stem.rfind('.');
    return dot == std::string_view::npos ? std::string_view() : stem.substr(dot + 1);
}

} // namespace

IncludeResolver::IncludeResolver(std::vector<std::filesystem::path> searchPaths,
                                 std::vector<std::filesystem::path> allowedRoots)
    : searchPaths(std::move(searchPaths)), allowedRoots(std::move(allowedRoots)) {
    for (std::filesystem::path& root : this->allowedRoots) {
        root = canonicalize(root);
    }
}

bool IncludeResolver::isAllowed(const std::filesystem::path& path) const {
    if (allowedRoots.empty()) {
        return true;
    }
    return std::ranges::any_of(
        allowedRoots, [&path](const std::filesystem::path& root) { return isInside(path, root); });
}

const std::vector<std::string>& IncludeResolver::listing(const std::filesystem::path& directory) {
    const auto found = listings.find(directory);
    if (found != listings.end()) {
        return found->second;
    }

    std::vector<std::string> names;
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        names.push_back(entry->path().filename().string());
    }
    std::ranges::sort(names);
    return listings.emplace(directory, std::move(names)).first->second;
}

bool IncludeResolver::holds(const std::filesystem::path& file) {
    const std::vector<std::string>& names = listing(file.parent_path());
    return std::ranges::binary_search(names, file.filename().string());
}

std::string IncludeResolver::startingWith(const std::filesystem::path& directory,
                                          std::string_view prefix, std::string_view extension) {
    const std::vector<std::string>& names = listing(directory);
    for (auto candidate = std::ranges::lower_bound(names, prefix);
         candidate != names.end() && std::string_view(*candidate).starts_with(prefix);
         ++candidate) {
        if (candidate->ends_with(extension)) {
            return *candidate;
        }
    }
    return {};
}

std::expected<ResolvedInclude, std::string>
IncludeResolver::resolveImage(const std::filesystem::path& candidate) {
    const std::filesystem::path directory = candidate.parent_path();
    const std::string name = candidate.filename().string();
    const std::string stem = name.substr(0, name.size() - GeneratedSuffix.size());
    const std::string format(formatOf(stem));

    if (format == "bin" && holds(directory / stem)) {
        return ResolvedInclude{.path = directory / stem, .isImage = true, .format = "bin"};
    }

    // A paletted texture's image carries in its name the palette it was
    // extracted against, so the include's spelling is only a prefix of it.
    const std::string image = startingWith(directory, stem, ".png");
    if (image.empty()) {
        return std::unexpected(std::format("no image for '{}'", name));
    }
    return ResolvedInclude{.path = directory / image, .isImage = true, .format = format};
}

std::expected<ResolvedInclude, std::string>
IncludeResolver::resolve(std::string_view spelling,
                         const std::filesystem::path& includingDirectory) {
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(canonicalize(includingDirectory / std::filesystem::path(spelling)));
    for (const std::filesystem::path& root : searchPaths) {
        candidates.push_back(canonicalize(root / std::filesystem::path(spelling)));
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (!holds(candidate)) {
            continue;
        }
        if (!isAllowed(candidate)) {
            return std::unexpected(std::format("'{}' is outside the asset tree", spelling));
        }
        return ResolvedInclude{.path = candidate};
    }

    if (spelling.ends_with(GeneratedSuffix)) {
        for (const std::filesystem::path& candidate : candidates) {
            std::expected<ResolvedInclude, std::string> image = resolveImage(candidate);
            if (!image) {
                continue;
            }
            if (!isAllowed(image->path)) {
                return std::unexpected(std::format("'{}' is outside the asset tree", spelling));
            }
            return image;
        }
    }
    return std::unexpected(std::format("cannot find '{}'", spelling));
}

} // namespace z64
