#pragma once

#include "cparser/DirectoryCache.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {

struct ResolvedInclude {
    std::filesystem::path path;

    /// The include stands for a file that is not read as C, such as data the
    /// build generates from it. It is kept as one `Embedded` token, and `tag`
    /// says what kind of file it is in whatever terms the caller chose.
    bool embedded = false;
    std::string tag;
};

/// Asked about an include that names no file on disk, once for each path it
/// could have meant. It may answer with what the build would have made in that
/// file's place; nothing means the include is missing.
using MissingInclude = std::function<std::optional<ResolvedInclude>(
    const std::filesystem::path& candidate, DirectoryCache& directories)>;

/// Turns an `#include` spelling into a file on disk, under a policy: an include
/// is followed only when it lands inside one of the allowed roots, so a scene
/// pulls in its rooms and never descends into `z64.h`. Every question about the
/// filesystem goes through one `DirectoryCache`.
class IncludeResolver {
public:
    IncludeResolver(std::vector<std::filesystem::path> searchPaths,
                    std::vector<std::filesystem::path> allowedRoots, MissingInclude missing);

    [[nodiscard]] std::expected<ResolvedInclude, std::string>
    resolve(std::string_view spelling, const std::filesystem::path& includingDirectory);

private:
    [[nodiscard]] bool isAllowed(const std::filesystem::path& path) const;

    std::vector<std::filesystem::path> searchPaths;
    std::vector<std::filesystem::path> allowedRoots;
    MissingInclude missing;
    DirectoryCache directories;
};

} // namespace cparser
