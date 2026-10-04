#pragma once

#include "n64/Types.h"

#include <array>

namespace n64 {

enum class TextureFormat : u8 {
    Rgba = 0,
    Yuv = 1,
    ColorIndex = 2,
    IntensityAlpha = 3,
    Intensity = 4,
};

enum class TexelSize : u8 {
    Bits4 = 0,
    Bits8 = 1,
    Bits16 = 2,
    Bits32 = 3,
};

/// What `G_MDSFT_TEXTLUT` in the other mode says about palettes.
enum class TlutMode : u8 {
    None = 0,
    Rgba16 = 2,
    IntensityAlpha16 = 3,
};

/// `G_SETTIMG`: where the RDP loads texels from.
struct TextureImage {
    TextureFormat format = TextureFormat::Rgba;
    TexelSize size = TexelSize::Bits4;
    u32 width = 1;
    /// Physical address.
    u32 address = 0;
};

/// One axis of a tile: how a coordinate is shifted, clamped, wrapped and
/// mirrored, and the tile's extent along it in 10.2 fixed point.
struct TileAxis {
    bool clamp = false;
    bool mirror = false;
    u8 mask = 0;
    u8 shift = 0;
    u16 low = 0;
    u16 high = 0;

    friend bool operator==(const TileAxis&, const TileAxis&) = default;
};

/// One of the RDP's eight tile descriptors, as `G_SETTILE` and
/// `G_SETTILESIZE` leave it.
struct TileDescriptor {
    TextureFormat format = TextureFormat::Rgba;
    TexelSize size = TexelSize::Bits4;
    /// Bytes per row, in 64-bit words.
    u16 line = 0;
    /// Where the tile starts in texture memory, in 64-bit words.
    u16 address = 0;
    u8 palette = 0;
    /// S, then T.
    std::array<TileAxis, 2> axes{};

    friend bool operator==(const TileDescriptor&, const TileDescriptor&) = default;
};

} // namespace n64
