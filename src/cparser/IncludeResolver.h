#pragma once

#include "cparser/DirectoryCache.h"
#include "cparser/Types.h"

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

/// Why an include is not followed.
enum class IncludeFailure : u8 {
    /// It names no file in any search path, and nothing stands in for one.
    Missing,
    /// It names a file, but outside every allowed root: one the caller reads
    /// another way, as a scene's `z64.h` is read by the header scrape.
    OutsideRoots,
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
                    std::vector<std::filesystem::path> allowedRoots, MissingInclude resolveMissing);

    /// A file inside the allowed roots wins over a stand-in for a missing one,
    /// and a stand-in over a file outside them, so a texture include the build
    /// generated into `build/` still resolves to the image it came from.
    [[nodiscard]] std::expected<ResolvedInclude, IncludeFailure>
    resolve(std::string_view spelling, const std::filesystem::path& includingDirectory);

private:
    [[nodiscard]] bool isAllowed(const std::filesystem::path& path) const;

    std::vector<std::filesystem::path> searchPaths;
    std::vector<std::filesystem::path> allowedRoots;
    MissingInclude resolveMissing;
    DirectoryCache directories;
};

} // namespace cparser
