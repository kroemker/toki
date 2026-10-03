#include "IncludeResolver.h"

#include "cparser/DirectoryCache.h"
#include "cparser/SourceSet.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

IncludeResolver::IncludeResolver(std::vector<std::filesystem::path> searchPaths,
                                 std::vector<std::filesystem::path> allowedRoots,
                                 MissingInclude resolveMissing)
    : searchPaths(std::move(searchPaths)), allowedRoots(std::move(allowedRoots)),
      resolveMissing(std::move(resolveMissing)) {
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

std::expected<ResolvedInclude, std::string>
IncludeResolver::resolve(std::string_view spelling,
                         const std::filesystem::path& includingDirectory) {
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(canonicalize(includingDirectory / std::filesystem::path(spelling)));
    for (const std::filesystem::path& root : searchPaths) {
        candidates.push_back(canonicalize(root / std::filesystem::path(spelling)));
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (!directories.holds(candidate)) {
            continue;
        }
        if (!isAllowed(candidate)) {
            return std::unexpected(std::format("'{}' is outside the asset tree", spelling));
        }
        return ResolvedInclude{.path = candidate};
    }

    if (resolveMissing) {
        for (const std::filesystem::path& candidate : candidates) {
            std::optional<ResolvedInclude> stand = resolveMissing(candidate, directories);
            if (!stand) {
                continue;
            }
            if (!isAllowed(stand->path)) {
                return std::unexpected(std::format("'{}' is outside the asset tree", spelling));
            }
            return std::move(*stand);
        }
    }
    return std::unexpected(std::format("cannot find '{}'", spelling));
}

} // namespace cparser
