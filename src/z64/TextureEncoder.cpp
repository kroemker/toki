#include "TextureEncoder.h"

#include "cparser/DirectoryCache.h"
#include "n64/Color.h"
#include "n64/TexturePacking.h"
#include "n64/TileDescriptor.h"
#include "z64/PngImage.h"
#include "z64/TextureIncludes.h"
#include "z64/TextureName.h"
#include "z64/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr usize SplitPaletteColors = 128;
constexpr usize JpegBufferSize = usize{320} * 240 * 2;

usize findMaxColors(const TextureName& name) {
    if (name.size == n64::TexelSize::Bits4) {
        return 16;
    }
    return name.paletteHalf == PaletteHalf::Whole ? 256 : SplitPaletteColors;
}

std::string describe(const std::filesystem::path& path) {
    return path.filename().string();
}

std::vector<u8> padBytes(std::vector<u8> bytes, const usize alignment) {
    bytes.resize((bytes.size() + alignment - 1) / alignment * alignment, 0);
    return bytes;
}

std::expected<std::vector<u8>, std::string> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("{} could not be read", describe(path)));
    }
    return std::vector<u8>((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
}

std::expected<std::vector<n64::Color>, std::string> readPalette(const std::filesystem::path& path) {
    const std::expected<std::vector<u8>, std::string> bytes = readFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::expected<std::vector<n64::Color>, std::string> palette = decodePngPalette(*bytes);
    if (!palette) {
        return std::unexpected(std::format("{}: {}", describe(path), palette.error()));
    }
    return palette;
}

std::expected<TextureName, std::string> readName(const std::filesystem::path& path) {
    const std::optional<TextureName> name = parseTextureName(path.filename().string());
    if (!name) {
        return std::unexpected(
            std::format("{} does not name a texture format the build knows", describe(path)));
    }
    return *name;
}

std::expected<std::vector<u8>, std::string> encodeJpeg(const std::filesystem::path& path) {
    std::expected<std::vector<u8>, std::string> bytes = readFile(path);
    if (!bytes) {
        return bytes;
    }
    if (bytes->size() > JpegBufferSize) {
        return std::unexpected(
            std::format("{} is {} bytes, more than the {} a JPEG background holds", describe(path),
                        bytes->size(), JpegBufferSize));
    }
    bytes->resize(JpegBufferSize, 0);
    return bytes;
}

bool isSharingAlike(const TextureName& name, const TextureName& other) {
    return name.size == other.size && name.paletteHalf == other.paletteHalf &&
           name.paletteElementSize == other.paletteElementSize;
}

std::optional<std::filesystem::path> findRelative(const std::filesystem::path& directory,
                                                  const std::filesystem::path& root) {
    std::filesystem::path relative =
        directory.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return relative;
}

} // namespace

std::optional<std::pair<u32, u32>>
TextureEncoder::findImageSize(const std::filesystem::path& path) const {
    const auto found = imageSizes.find(path);
    return found == imageSizes.end() ? std::nullopt : std::optional(found->second);
}

TextureEncoder::TextureEncoder(cparser::DirectoryCache& directories,
                               std::vector<std::filesystem::path> assetRoots)
    : directories(directories), assetRoots(std::move(assetRoots)) {}

std::expected<std::vector<u8>, std::string>
TextureEncoder::encode(const std::filesystem::path& path, const std::string_view tag) {
    if (tag == BinaryTag) {
        return readFile(path);
    }
    if (tag == JpegTag) {
        return encodeJpeg(path);
    }
    const std::expected<TextureName, std::string> name = readName(path);
    if (!name) {
        return std::unexpected(name.error());
    }
    if (tag == PaletteTag) {
        return encodePalette(path, *name);
    }
    return encodeImage(path, *name);
}

std::expected<std::vector<u8>, std::string>
TextureEncoder::encodeImage(const std::filesystem::path& path, const TextureName& name) {
    const std::expected<std::vector<u8>, std::string> bytes = readFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const std::expected<PngImage, std::string> image = decodePng(*bytes);
    if (!image) {
        return std::unexpected(std::format("{}: {}", describe(path), image.error()));
    }
    imageSizes.insert_or_assign(path, std::pair(image->width, image->height));
    if (name.format != n64::TextureFormat::ColorIndex) {
        return padBytes(n64::packTexels(image->texels, name.format, name.size), name.elementSize);
    }

    if (image->palette.empty()) {
        return std::unexpected(std::format(
            "{} is colour-indexed but not a palette-based PNG; the build would quantize it, "
            "which is not supported",
            describe(path)));
    }
    if (image->palette.size() > findMaxColors(name)) {
        return std::unexpected(std::format(
            "{} has {} colours, more than its format holds; the build would quantize it, which "
            "is not supported",
            describe(path), image->palette.size()));
    }
    if (name.sharedPalette) {
        const std::expected<const std::vector<n64::Color>*, std::string> shared =
            findSharedPalette(path.parent_path(), name);
        if (!shared) {
            return std::unexpected(shared.error());
        }
    }
    std::vector<u8> indices = image->indices;
    if (name.paletteHalf == PaletteHalf::High) {
        std::ranges::transform(indices, indices.begin(), [](const u8 index) {
            return static_cast<u8>(index + SplitPaletteColors);
        });
    }
    return padBytes(n64::packColorIndices(indices, name.size), name.elementSize);
}

std::expected<std::vector<u8>, std::string>
TextureEncoder::encodePalette(const std::filesystem::path& path, const TextureName& name) {
    if (!name.sharedPalette) {
        const std::expected<std::vector<n64::Color>, std::string> palette = readPalette(path);
        if (!palette) {
            return std::unexpected(palette.error());
        }
        if (palette->size() > findMaxColors(name)) {
            return std::unexpected(std::format(
                "{} has {} colours, more than its format holds; the build would quantize it, "
                "which is not supported",
                describe(path), palette->size()));
        }
        return n64::packPalette(*palette);
    }
    const std::expected<const std::vector<n64::Color>*, std::string> shared =
        findSharedPalette(path.parent_path(), name);
    if (!shared) {
        return std::unexpected(shared.error());
    }
    std::vector<n64::Color> colors = **shared;
    if (name.paletteHalf != PaletteHalf::Whole) {
        colors.resize(SplitPaletteColors, n64::Color{0, 0, 0, 0});
    }
    return padBytes(n64::packPalette(colors), name.paletteElementSize);
}

std::expected<const std::vector<n64::Color>*, std::string>
TextureEncoder::findSharedPalette(const std::filesystem::path& directory, const TextureName& name) {
    const std::string palette = name.sharedPalette.value_or(std::string());
    const auto key = std::make_pair(directory, palette);
    if (const auto found = palettes.find(key); found != palettes.end()) {
        return &found->second;
    }
    std::optional<std::vector<n64::Color>> reference;
    for (const std::filesystem::path& file : listOverlaidFiles(directory)) {
        const std::optional<TextureName> other = parseTextureName(file.filename().string());
        if (!other || other->format != n64::TextureFormat::ColorIndex ||
            other->sharedPalette != palette) {
            continue;
        }
        if (!isSharingAlike(name, *other)) {
            return std::unexpected(
                std::format("the images sharing palette {} differ in format, split or palette "
                            "element size; {} is one",
                            palette, describe(file)));
        }
        std::expected<std::vector<n64::Color>, std::string> colors = readPalette(file);
        if (!colors) {
            return std::unexpected(colors.error());
        }
        if (colors->empty()) {
            return std::unexpected(std::format(
                "{} shares palette {} but is not a palette-based PNG; the build would quantize "
                "it, which is not supported",
                describe(file), palette));
        }
        if (!reference) {
            reference = std::move(*colors);
        } else if (*colors != *reference) {
            return std::unexpected(std::format(
                "the images sharing palette {} disagree on it; the build would merge them with "
                "libimagequant, which is not supported",
                palette));
        }
    }
    if (!reference) {
        return std::unexpected(std::format("no image shares palette {}", palette));
    }
    if (reference->size() > findMaxColors(name)) {
        return std::unexpected(std::format("palette {} has {} colours, more than its images hold",
                                           palette, reference->size()));
    }
    return &palettes.emplace(key, std::move(*reference)).first->second;
}

std::vector<std::filesystem::path>
TextureEncoder::listOverlaidFiles(const std::filesystem::path& directory) {
    std::optional<std::filesystem::path> relative;
    for (const std::filesystem::path& root : assetRoots) {
        relative = findRelative(directory, root);
        if (relative) {
            break;
        }
    }
    std::vector<std::filesystem::path> overlaid;
    if (!relative) {
        for (const std::string& file : directories.getListing(directory)) {
            overlaid.push_back(directory / file);
        }
        return overlaid;
    }
    std::set<std::string> seen;
    for (const std::filesystem::path& root : assetRoots) {
        const std::filesystem::path layer = root / *relative;
        for (const std::string& file : directories.getListing(layer)) {
            if (seen.insert(file).second) {
                overlaid.push_back(layer / file);
            }
        }
    }
    return overlaid;
}

} // namespace z64
