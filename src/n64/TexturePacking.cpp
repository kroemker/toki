#include "TexturePacking.h"

#include "n64/Color.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <span>
#include <vector>

namespace n64 {

namespace {

constexpr usize PaletteAlignment = 8;

void appendHalf(std::vector<u8>& bytes, const u32 half) {
    bytes.push_back(static_cast<u8>(half >> 8));
    bytes.push_back(static_cast<u8>(half));
}

u32 packRgba16(const Color& color) {
    const u32 red = color[0] >> 3;
    const u32 green = color[1] >> 3;
    const u32 blue = color[2] >> 3;
    const u32 alpha = color[3] != 0 ? 1 : 0;
    return (red << 11) | (green << 6) | (blue << 1) | alpha;
}

u8 packNibble(const Color& color, const TextureFormat format) {
    const u8 grey = makeGreyscale(color);
    if (format == TextureFormat::IntensityAlpha) {
        return static_cast<u8>(((grey >> 5) << 1) | (color[3] != 0 ? 1 : 0));
    }
    return static_cast<u8>(grey >> 4);
}

u8 packByte(const Color& color, const TextureFormat format) {
    if (format == TextureFormat::IntensityAlpha) {
        return static_cast<u8>(((makeGreyscale(color) >> 4) << 4) | (color[3] >> 4));
    }
    return makeGreyscale(color);
}

std::vector<u8> packNibbles(const std::span<const u8> values) {
    std::vector<u8> bytes((values.size() + 1) / 2, 0);
    for (usize i = 0; i < values.size(); ++i) {
        bytes[i / 2] |= static_cast<u8>((values[i] & 0x0F) << ((i % 2) == 0 ? 4 : 0));
    }
    return bytes;
}

} // namespace

u8 makeGreyscale(const Color& color) {
    return static_cast<u8>(((color[0] * 54) + (color[1] * 183) + (color[2] * 19) + 128) / 256);
}

std::vector<u8> packTexels(const std::span<const Color> texels, const TextureFormat format,
                           const TexelSize size) {
    std::vector<u8> bytes;
    switch (size) {
    case TexelSize::Bits4: {
        std::vector<u8> nibbles;
        nibbles.reserve(texels.size());
        for (const Color& texel : texels) {
            nibbles.push_back(packNibble(texel, format));
        }
        return packNibbles(nibbles);
    }
    case TexelSize::Bits8:
        bytes.reserve(texels.size());
        for (const Color& texel : texels) {
            bytes.push_back(packByte(texel, format));
        }
        return bytes;
    case TexelSize::Bits16:
        bytes.reserve(texels.size() * 2);
        for (const Color& texel : texels) {
            appendHalf(bytes, format == TextureFormat::IntensityAlpha
                                  ? (u32{makeGreyscale(texel)} << 8) | texel[3]
                                  : packRgba16(texel));
        }
        return bytes;
    case TexelSize::Bits32:
        bytes.reserve(texels.size() * 4);
        for (const Color& texel : texels) {
            bytes.insert(bytes.end(), texel.begin(), texel.end());
        }
        return bytes;
    }
    return bytes;
}

std::vector<u8> packColorIndices(const std::span<const u8> indices, const TexelSize size) {
    if (size == TexelSize::Bits4) {
        return packNibbles(indices);
    }
    return {indices.begin(), indices.end()};
}

std::vector<u8> packPalette(const std::span<const Color> colors) {
    std::vector<u8> bytes;
    bytes.reserve(colors.size() * 2);
    for (const Color& color : colors) {
        appendHalf(bytes, packRgba16(color));
    }
    while (bytes.size() % PaletteAlignment != 0) {
        bytes.push_back(0);
    }
    return bytes;
}

} // namespace n64
