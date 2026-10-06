#include "TextureCatalog.h"

#include "app/texture/Types.h"
#include "n64/Color.h"
#include "n64/TextureUnpacking.h"
#include "n64/TileDescriptor.h"
#include "z64/ImageTexture.h"
#include "z64/MemoryImage.h"
#include "z64/TextureName.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::texture {

namespace {

constexpr usize SplitPaletteColors = 128;

[[nodiscard]] std::optional<PaletteSource> findPalette(const z64::MemoryImage& image,
                                                       const std::string& paletteName) {
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.texture && symbol.texture->isPalette &&
            symbol.texture->paletteName == paletteName) {
            return PaletteSource{.name = name,
                                 .image = &image,
                                 .address = image.findAddress(name).value_or(0),
                                 .size = symbol.size};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<n64::Color> lookUpColors(const std::span<const u8> indices,
                                                   const std::span<const n64::Color> colors,
                                                   const z64::PaletteHalf half) {
    std::vector<n64::Color> pixels;
    pixels.reserve(indices.size());
    for (const u8 index : indices) {
        usize entry = index;
        if (half == z64::PaletteHalf::High && entry >= SplitPaletteColors) {
            entry -= SplitPaletteColors;
        }
        pixels.push_back(entry < colors.size() ? colors[entry] : n64::Color{0, 0, 0, 0});
    }
    return pixels;
}

[[nodiscard]] std::vector<n64::Color> makeGreys(const std::span<const u8> indices,
                                                const n64::TexelSize size) {
    const u32 scale = size == n64::TexelSize::Bits4 ? 0x11 : 1;
    std::vector<n64::Color> pixels;
    pixels.reserve(indices.size());
    for (const u8 index : indices) {
        const auto grey = static_cast<u8>(index * scale);
        pixels.push_back({grey, grey, grey, 0xFF});
    }
    return pixels;
}

} // namespace

std::vector<TextureEntry>
listTextures(const z64::MemoryImage& image,
             const std::span<const z64::MemoryImage* const> paletteImages) {
    std::vector<TextureEntry> entries;
    for (const auto& [name, symbol] : image.symbols) {
        if (!symbol.texture || symbol.texture->isPalette) {
            continue;
        }
        TextureEntry entry{.name = name,
                           .address = image.findAddress(name).value_or(0),
                           .size = symbol.size,
                           .texture = *symbol.texture};
        if (entry.texture.format == n64::TextureFormat::ColorIndex) {
            entry.palette = findPalette(image, entry.texture.paletteName);
            for (const z64::MemoryImage* other : paletteImages) {
                if (entry.palette) {
                    break;
                }
                entry.palette = findPalette(*other, entry.texture.paletteName);
            }
        }
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, {}, &TextureEntry::address);
    return entries;
}

std::expected<DecodedTexture, std::string> decodeTexture(const z64::MemoryImage& image,
                                                         const TextureEntry& entry) {
    const z64::ImageTexture& texture = entry.texture;
    if (texture.width == 0 || texture.height == 0) {
        return std::unexpected(std::format("the size of {} is not known", entry.name));
    }
    const std::optional<std::span<const u8>> bytes = image.findBytes(entry.address, entry.size);
    if (!bytes) {
        return std::unexpected(std::format("{} is not in the image", entry.name));
    }
    DecodedTexture decoded{.width = texture.width, .height = texture.height};
    const usize count = usize{texture.width} * texture.height;
    if (texture.format != n64::TextureFormat::ColorIndex) {
        decoded.pixels = n64::unpackTexels(*bytes, texture.format, texture.size, count);
        return decoded;
    }
    const std::vector<u8> indices = n64::unpackColorIndices(*bytes, texture.size, count);
    if (!entry.palette) {
        decoded.pixels = makeGreys(indices, texture.size);
        decoded.note = std::format("no palette named {} was found; its indices show as grey",
                                   texture.paletteName);
        return decoded;
    }
    const std::optional<std::span<const u8>> paletteBytes =
        entry.palette->image->findBytes(entry.palette->address, entry.palette->size);
    if (!paletteBytes) {
        decoded.pixels = makeGreys(indices, texture.size);
        decoded.note = std::format("{} is not in the image", entry.palette->name);
        return decoded;
    }
    const std::vector<n64::Color> colors = n64::unpackPalette(*paletteBytes, n64::TlutMode::Rgba16);
    decoded.pixels = lookUpColors(indices, colors, texture.paletteHalf);
    return decoded;
}

std::vector<u8> makeScaledPixels(const DecodedTexture& texture, const u32 scale) {
    const usize width = usize{texture.width} * scale;
    const usize height = usize{texture.height} * scale;
    std::vector<u8> bytes(width * height * 4);
    for (usize y = 0; y < height; ++y) {
        for (usize x = 0; x < width; ++x) {
            const usize source = ((y / scale) * texture.width) + (x / scale);
            const n64::Color& color = texture.pixels[source];
            std::ranges::copy(color,
                              bytes.begin() + static_cast<std::ptrdiff_t>(((y * width) + x) * 4));
        }
    }
    return bytes;
}

std::string getFormatName(const z64::ImageTexture& texture) {
    std::string name;
    switch (texture.format) {
    case n64::TextureFormat::Rgba:
        name = "rgba";
        break;
    case n64::TextureFormat::Yuv:
        name = "yuv";
        break;
    case n64::TextureFormat::ColorIndex:
        name = "ci";
        break;
    case n64::TextureFormat::IntensityAlpha:
        name = "ia";
        break;
    case n64::TextureFormat::Intensity:
        name = "i";
        break;
    }
    return name + std::to_string(4U << static_cast<u32>(texture.size));
}

} // namespace toki::texture
