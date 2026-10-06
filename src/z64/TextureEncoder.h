#pragma once

#include "cparser/DirectoryCache.h"
#include "n64/Color.h"
#include "z64/TextureName.h"
#include "z64/Types.h"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

/// Makes the bytes the build generates for an embedded file: exactly the
/// contents of the array its `.inc.c` fills, padding included, as the
/// decomp's `build_from_png`, `bin2c` and `n64texconv` write them. Shared
/// palettes are gathered once and kept.
///
/// What the build would do with libimagequant is not reproduced: a
/// colour-indexed image that is not a palette-based PNG, or has too many
/// colours, and images that share a palette but disagree on it, all fail
/// saying so.
class TextureEncoder {
public:
    /// `assetRoots` are directories that overlay one another, the first
    /// winning a name both hold, as the build's `assets/` overlays
    /// `extracted/<version>/assets/`. A shared palette is gathered from the
    /// same directory under every root. Without roots, it is gathered from the
    /// image's own directory only.
    explicit TextureEncoder(cparser::DirectoryCache& directories,
                            std::vector<std::filesystem::path> assetRoots = {});

    /// The bytes for `path` as `resolveTextureInclude` tagged it. Fails with a
    /// reason naming the file.
    [[nodiscard]] std::expected<std::vector<u8>, std::string>
    encode(const std::filesystem::path& path, std::string_view tag);

    /// The width and height of an image `encode` has read, or nothing.
    [[nodiscard]] std::optional<std::pair<u32, u32>>
    findImageSize(const std::filesystem::path& path) const;

private:
    [[nodiscard]] std::expected<std::vector<u8>, std::string>
    encodeImage(const std::filesystem::path& path, const TextureName& name);
    [[nodiscard]] std::expected<std::vector<u8>, std::string>
    encodePalette(const std::filesystem::path& path, const TextureName& name);
    [[nodiscard]] std::expected<const std::vector<n64::Color>*, std::string>
    findSharedPalette(const std::filesystem::path& directory, const TextureName& name);
    [[nodiscard]] std::vector<std::filesystem::path>
    listOverlaidFiles(const std::filesystem::path& directory);

    cparser::DirectoryCache& directories;
    std::vector<std::filesystem::path> assetRoots;
    std::map<std::pair<std::filesystem::path, std::string>, std::vector<n64::Color>> palettes;
    std::map<std::filesystem::path, std::pair<u32, u32>> imageSizes;
};

} // namespace z64
