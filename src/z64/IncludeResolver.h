#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

struct ResolvedInclude {
    std::filesystem::path path;

    /// Extracted assets include texture data from a `.inc.c` the build
    /// generates out of a `.png`, so the file named in the source never exists
    /// in the checkout. When that is what happened, `path` is the image and
    /// `format` is the N64 format its name carries.
    bool isImage = false;
    std::string format;
};

/// Turns an `#include` spelling into a file on disk, under a policy: an include
/// is followed only when it lands inside one of the allowed roots, so a scene
/// pulls in its rooms and never descends into `z64.h`.
///
/// Every question is answered from a cached listing of the directory rather
/// than by asking about one file at a time. A checkout on a network share
/// charges milliseconds per query, and a vanilla scene asks hundreds.
class IncludeResolver {
public:
    IncludeResolver(std::vector<std::filesystem::path> searchPaths,
                    std::vector<std::filesystem::path> allowedRoots);

    [[nodiscard]] std::expected<ResolvedInclude, std::string>
    resolve(std::string_view spelling, const std::filesystem::path& includingDirectory);

private:
    [[nodiscard]] const std::vector<std::string>& listing(const std::filesystem::path& directory);

    [[nodiscard]] bool holds(const std::filesystem::path& file);

    [[nodiscard]] std::string startingWith(const std::filesystem::path& directory,
                                           std::string_view prefix, std::string_view extension);

    [[nodiscard]] bool isAllowed(const std::filesystem::path& path) const;

    [[nodiscard]] std::expected<ResolvedInclude, std::string>
    resolveImage(const std::filesystem::path& candidate);

    std::vector<std::filesystem::path> searchPaths;
    std::vector<std::filesystem::path> allowedRoots;
    std::map<std::filesystem::path, std::vector<std::string>> listings;
};

} // namespace z64
