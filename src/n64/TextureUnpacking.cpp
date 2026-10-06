#include "TextureUnpacking.h"

#include "n64/Color.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <span>
#include <vector>

namespace n64 {

namespace {

constexpr Color Transparent{0, 0, 0, 0};

[[nodiscard]] u8 expandFive(const u32 value) {
    return static_cast<u8>((value << 3) | (value >> 2));
}

[[nodiscard]] u8 expandThree(const u32 value) {
    return static_cast<u8>((value << 5) | (value << 2) | (value >> 1));
}

[[nodiscard]] Color makeGrey(const u8 intensity, const u8 alpha) {
    return {intensity, intensity, intensity, alpha};
}

[[nodiscard]] Color decodeNibble(const u32 nibble, const TextureFormat format) {
    if (format == TextureFormat::IntensityAlpha) {
        return makeGrey(expandThree(nibble >> 1), (nibble & 1) != 0 ? 0xFF : 0);
    }
    const auto intensity = static_cast<u8>(nibble * 0x11);
    return makeGrey(intensity, intensity);
}

[[nodiscard]] Color decodeByte(const u32 value, const TextureFormat format) {
    if (format == TextureFormat::IntensityAlpha) {
        return makeGrey(static_cast<u8>((value >> 4) * 0x11),
                        static_cast<u8>((value & 0x0F) * 0x11));
    }
    return makeGrey(static_cast<u8>(value), static_cast<u8>(value));
}

[[nodiscard]] u32 readHalf(const std::span<const u8> bytes, const usize at) {
    return (u32{bytes[at]} << 8) | bytes[at + 1];
}

[[nodiscard]] usize findBits(const TexelSize size) {
    return usize{4} << static_cast<usize>(size);
}

} // namespace

Color decodeRgba16(const u32 value) {
    return {expandFive((value >> 11) & 0x1F), expandFive((value >> 6) & 0x1F),
            expandFive((value >> 1) & 0x1F), static_cast<u8>((value & 1) != 0 ? 0xFF : 0)};
}

Color decodeIntensityAlpha16(const u32 value) {
    return makeGrey(static_cast<u8>(value >> 8), static_cast<u8>(value));
}

std::vector<Color> unpackTexels(const std::span<const u8> bytes, const TextureFormat format,
                                const TexelSize size, const usize count) {
    std::vector<Color> texels(count, Transparent);
    const usize bits = findBits(size);
    for (usize i = 0; i < count; ++i) {
        const usize at = (i * bits) / 8;
        if (at + ((bits + 7) / 8) > bytes.size()) {
            break;
        }
        switch (size) {
        case TexelSize::Bits4:
            texels[i] = decodeNibble((bytes[at] >> ((i % 2) == 0 ? 4 : 0)) & 0x0F, format);
            break;
        case TexelSize::Bits8:
            texels[i] = decodeByte(bytes[at], format);
            break;
        case TexelSize::Bits16:
            texels[i] = format == TextureFormat::IntensityAlpha
                            ? decodeIntensityAlpha16(readHalf(bytes, at))
                            : decodeRgba16(readHalf(bytes, at));
            break;
        case TexelSize::Bits32:
            texels[i] = {bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]};
            break;
        }
    }
    return texels;
}

std::vector<u8> unpackColorIndices(const std::span<const u8> bytes, const TexelSize size,
                                   const usize count) {
    std::vector<u8> indices(count, 0);
    for (usize i = 0; i < count; ++i) {
        if (size == TexelSize::Bits4) {
            if (i / 2 >= bytes.size()) {
                break;
            }
            indices[i] = static_cast<u8>((bytes[i / 2] >> ((i % 2) == 0 ? 4 : 0)) & 0x0F);
        } else {
            if (i >= bytes.size()) {
                break;
            }
            indices[i] = bytes[i];
        }
    }
    return indices;
}

std::vector<Color> unpackPalette(const std::span<const u8> bytes, const TlutMode tlut) {
    std::vector<Color> colors;
    colors.reserve(bytes.size() / 2);
    for (usize at = 0; at + 1 < bytes.size(); at += 2) {
        const u32 value = readHalf(bytes, at);
        colors.push_back(tlut == TlutMode::IntensityAlpha16 ? decodeIntensityAlpha16(value)
                                                            : decodeRgba16(value));
    }
    return colors;
}

} // namespace n64
