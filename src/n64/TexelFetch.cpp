#include "TexelFetch.h"

#include "n64/Color.h"
#include "n64/OtherMode.h"
#include "n64/TextureMemory.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace n64 {

namespace {

constexpr u32 FullMask = 0x0FFF;
constexpr u32 HalfMask = 0x07FF;
constexpr u32 PaletteBase = 0x0800;
constexpr u8 MaxMask = 10;

s32 shiftCoordinate(const s32 value, const u8 shift) {
    if (shift <= 10) {
        return value >> shift;
    }
    return static_cast<s32>(static_cast<u32>(value) << (16U - shift));
}

s32 wrapModulo(const s32 value, const s32 size) {
    const s32 remainder = value % size;
    return remainder < 0 ? remainder + size : remainder;
}

s32 wrapAxis(const TileAxis& axis, s32 value) {
    if (axis.clamp || axis.mask == 0) {
        const s32 maximum = axis.high >= axis.low ? (axis.high - axis.low) >> 2 : 0;
        value = std::clamp(value, 0, maximum);
    }
    if (axis.mask == 0) {
        return value;
    }
    const s32 size = 1 << std::min(axis.mask, MaxMask);
    if (axis.mirror && wrapModulo(value, size * 2) >= size) {
        return size - 1 - wrapModulo(value, size);
    }
    return wrapModulo(value, size);
}

u8 expandFive(const u32 value) {
    return static_cast<u8>((value << 3) | (value >> 2));
}

Color decodeRgba16(const u32 value) {
    return {expandFive((value >> 11) & 0x1F), expandFive((value >> 6) & 0x1F),
            expandFive((value >> 1) & 0x1F), static_cast<u8>((value & 1) != 0 ? 0xFF : 0)};
}

Color decodeIntensityAlpha16(const u32 value) {
    const auto intensity = static_cast<u8>(value >> 8);
    return {intensity, intensity, intensity, static_cast<u8>(value)};
}

Color makeGray(const u32 intensity) {
    const auto value = static_cast<u8>(intensity);
    return {value, value, value, value};
}

Color decode4(const TileDescriptor& tile, const u32 nibble) {
    switch (tile.format) {
    case TextureFormat::ColorIndex:
        return makeGray((u32{tile.palette} << 4) | nibble);
    case TextureFormat::IntensityAlpha: {
        const u32 level = nibble >> 1;
        const auto intensity = static_cast<u8>((level << 5) | (level << 2) | (level >> 1));
        return {intensity, intensity, intensity, static_cast<u8>((nibble & 1) != 0 ? 0xFF : 0)};
    }
    case TextureFormat::Intensity:
    case TextureFormat::Rgba:
        return makeGray(nibble * 0x11);
    default:
        return {0, 0, 0, 0xFF};
    }
}

Color decode8(const TileDescriptor& tile, const u32 value) {
    switch (tile.format) {
    case TextureFormat::IntensityAlpha: {
        const auto intensity = static_cast<u8>((value >> 4) * 0x11);
        return {intensity, intensity, intensity, static_cast<u8>((value & 0x0F) * 0x11)};
    }
    case TextureFormat::Intensity:
    case TextureFormat::Rgba:
    case TextureFormat::ColorIndex:
        return makeGray(value);
    default:
        return {0, 0, 0, 0xFF};
    }
}

Color decode16(const TileDescriptor& tile, const u32 high, const u32 low) {
    switch (tile.format) {
    case TextureFormat::Rgba:
        return decodeRgba16((high << 8) | low);
    case TextureFormat::IntensityAlpha:
        return decodeIntensityAlpha16((high << 8) | low);
    case TextureFormat::ColorIndex:
    case TextureFormat::Intensity:
        return {static_cast<u8>(high), static_cast<u8>(low), static_cast<u8>(high),
                static_cast<u8>(low)};
    default:
        return {0, 0, 0, 0xFF};
    }
}

Color decode32(const TileDescriptor& tile, const std::array<u8, 4>& values, const bool oddColumn) {
    switch (tile.format) {
    case TextureFormat::Rgba:
        return values;
    case TextureFormat::ColorIndex:
    case TextureFormat::IntensityAlpha:
    case TextureFormat::Intensity:
        return oddColumn ? Color{values[0], values[1], values[0], values[1]}
                         : Color{values[2], values[3], values[2], values[3]};
    default:
        return {0, 0, 0, 0xFF};
    }
}

} // namespace

Texel findTileCoordinate(const TileDescriptor& tile, const std::array<f32, 2> coordinate) {
    Texel result{};
    for (usize axis = 0; axis < 2; ++axis) {
        const TileAxis& tileAxis = tile.axes[axis];
        const auto fixedPoint = static_cast<s32>(std::floor(coordinate[axis] * 32.0F));
        result[axis] = shiftCoordinate(fixedPoint, tileAxis.shift) - (s32{tileAxis.low} << 3);
    }
    return result;
}

Texel findTileTexel(const TileDescriptor& tile, const std::array<f32, 2> coordinate) {
    const Texel fixedPoint = findTileCoordinate(tile, coordinate);
    return {fixedPoint[0] >> 5, fixedPoint[1] >> 5};
}

Texel wrapTexel(const TileDescriptor& tile, const Texel texel) {
    return {wrapAxis(tile.axes[0], texel[0]), wrapAxis(tile.axes[1], texel[1])};
}

Color fetchTexel(const std::span<const u8, TextureMemorySize> memory, const TileDescriptor& tile,
                 const Texel texel, const TlutMode tlut) {
    const auto x = static_cast<u32>(texel[0]);
    const auto y = static_cast<u32>(texel[1]);
    const bool oddColumn = (x & 1) != 0;
    const u32 swap = (y & 1) != 0 ? 4 : 0;
    const bool split = tile.format == TextureFormat::Rgba && tile.size == TexelSize::Bits32;
    const bool palette = tlut != TlutMode::None;
    const u32 shift = split ? 2 : static_cast<u32>(tile.size);
    const u32 mask = split || palette ? HalfMask : FullMask;
    const u32 base = u32{tile.address} << 3;
    const u32 address = (y * (u32{tile.line} << 3)) + ((x << shift) >> 1);
    const auto load = [&](const u32 relative, const u32 upper) -> u32 {
        return memory[(((base + (relative ^ swap)) & mask) | upper) & FullMask];
    };

    const u32 first = load(address, 0);
    const u32 nibble = (first >> (oddColumn ? 0 : 4)) & 0x0F;
    if (palette) {
        const u32 entry = tile.size == TexelSize::Bits4
                              ? PaletteBase + (u32{tile.palette} << 7) + (nibble << 3)
                              : PaletteBase + (first << 3);
        const u32 value = (u32{memory[entry & FullMask]} << 8) | memory[(entry + 1) & FullMask];
        return tlut == TlutMode::IntensityAlpha16 ? decodeIntensityAlpha16(value)
                                                  : decodeRgba16(value);
    }

    const u32 second = load(address + 1, 0);
    switch (tile.size) {
    case TexelSize::Bits4:
        return decode4(tile, nibble);
    case TexelSize::Bits8:
        return decode8(tile, first);
    case TexelSize::Bits16:
        return decode16(tile, first, second);
    case TexelSize::Bits32: {
        const u32 upperAddress = split ? address : address + 2;
        const u32 upper = split ? PaletteBase : 0;
        return decode32(tile,
                        {static_cast<u8>(first), static_cast<u8>(second),
                         static_cast<u8>(load(upperAddress, upper)),
                         static_cast<u8>(load(upperAddress + 1, upper))},
                        oddColumn);
    }
    }
    return {0, 0, 0, 0xFF};
}

Color sampleTexel(const std::span<const u8, TextureMemorySize> memory, const TileDescriptor& tile,
                  const std::array<f32, 2> coordinate, const TlutMode tlut) {
    return fetchTexel(memory, tile, wrapTexel(tile, findTileTexel(tile, coordinate)), tlut);
}

Color sampleTexture(const std::span<const u8, TextureMemorySize> memory, const TileDescriptor& tile,
                    const std::array<f32, 2> coordinate, const TlutMode tlut,
                    const TextureFilter filter) {
    const Texel fixedPoint = findTileCoordinate(tile, coordinate);
    const Texel base{fixedPoint[0] >> 5, fixedPoint[1] >> 5};
    const Texel fraction{fixedPoint[0] & 31, fixedPoint[1] & 31};
    const auto fetchAt = [&](const s32 x, const s32 y) {
        return fetchTexel(memory, tile, wrapTexel(tile, {base[0] + x, base[1] + y}), tlut);
    };
    const Color t00 = fetchAt(0, 0);
    if (filter == TextureFilter::Point) {
        return t00;
    }
    const Color t10 = fetchAt(1, 0);
    const Color t01 = fetchAt(0, 1);
    const Color t11 = fetchAt(1, 1);
    Color result{};
    if (filter == TextureFilter::Average && fraction[0] == 16 && fraction[1] == 16) {
        for (usize channel = 0; channel < result.size(); ++channel) {
            result[channel] = static_cast<u8>(
                (t00[channel] + t10[channel] + t01[channel] + t11[channel] + 2) >> 2);
        }
        return result;
    }
    const bool upper = fraction[0] + fraction[1] >= 32;
    const Color& origin = upper ? t11 : t00;
    const s32 weightS = upper ? 32 - fraction[1] : fraction[0];
    const s32 weightT = upper ? 32 - fraction[0] : fraction[1];
    for (usize channel = 0; channel < result.size(); ++channel) {
        const s32 base0 = origin[channel];
        const s32 accumulated =
            ((t10[channel] - base0) * weightS) + ((t01[channel] - base0) * weightT) + 0x10;
        result[channel] = static_cast<u8>(base0 + (accumulated >> 5));
    }
    return result;
}

} // namespace n64
