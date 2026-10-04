#include "TextureIncludes.h"

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "n64/TileDescriptor.h"
#include "z64/TextureName.h"
#include "z64/Types.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace z64 {

namespace {

constexpr std::string_view GeneratedSuffix = ".inc.c";

struct PaletteSuffix {
    std::string_view suffix;
    usize elementSize;
};

constexpr std::array<PaletteSuffix, 2> PaletteSuffixes{{
    {".tlut.rgba16", 8},
    {".tlut.rgba16.u32", 4},
}};

struct RawSuffix {
    std::string_view suffix;
    std::string_view tag;
};

constexpr std::array<RawSuffix, 2> RawSuffixes{{
    {".jpg", JpegTag},
    {".bin", BinaryTag},
}};

bool isPaletteOf(const TextureName& name, const std::string_view palette, const usize elementSize) {
    if (name.format != n64::TextureFormat::ColorIndex) {
        return false;
    }
    if (name.sharedPalette) {
        return *name.sharedPalette == palette && name.paletteElementSize == elementSize;
    }
    return name.prefix == palette && elementSize == 8;
}

std::optional<std::string> findPaletteImage(const std::filesystem::path& directory,
                                            const std::string_view palette, const usize elementSize,
                                            cparser::DirectoryCache& directories) {
    for (const std::string& file : directories.getListing(directory)) {
        const std::optional<TextureName> name = parseTextureName(file);
        if (name && isPaletteOf(*name, palette, elementSize)) {
            return file;
        }
    }
    return std::nullopt;
}

std::optional<cparser::ResolvedInclude> makeEmbedded(std::filesystem::path path,
                                                     const std::string_view tag) {
    return cparser::ResolvedInclude{
        .path = std::move(path), .embedded = true, .tag = std::string(tag)};
}

} // namespace

std::optional<cparser::ResolvedInclude>
resolveTextureInclude(const std::filesystem::path& candidate,
                      cparser::DirectoryCache& directories) {
    const std::string name = candidate.filename().string();
    if (!name.ends_with(GeneratedSuffix)) {
        return std::nullopt;
    }
    const std::string stem = name.substr(0, name.size() - GeneratedSuffix.size());
    const std::filesystem::path directory = candidate.parent_path();

    for (const RawSuffix& raw : RawSuffixes) {
        if (stem.ends_with(raw.suffix) && directories.holds(directory / stem)) {
            return makeEmbedded(directory / stem, raw.tag);
        }
    }
    for (const PaletteSuffix& palette : PaletteSuffixes) {
        if (!stem.ends_with(palette.suffix)) {
            continue;
        }
        const std::string_view paletteName =
            std::string_view(stem).substr(0, stem.size() - palette.suffix.size());
        if (const std::optional<std::string> image =
                findPaletteImage(directory, paletteName, palette.elementSize, directories)) {
            return makeEmbedded(directory / *image, PaletteTag);
        }
    }
    if (!stem.contains('.')) {
        return std::nullopt;
    }
    const std::string image = directories.findStartingWith(directory, stem + ".", ".png");
    if (image.empty()) {
        return std::nullopt;
    }
    return makeEmbedded(directory / image, ImageTag);
}

} // namespace z64
