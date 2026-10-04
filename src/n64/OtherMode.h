#pragma once

#include "n64/TileDescriptor.h"
#include "n64/Types.h"

namespace n64 {

enum class CycleType : u8 {
    One = 0,
    Two = 1,
    Copy = 2,
    Fill = 3,
};

enum class TextureFilter : u8 {
    Point = 0,
    Bilinear = 2,
    Average = 3,
};

enum class DepthMode : u8 {
    Opaque = 0,
    Interpenetrating = 1,
    Translucent = 2,
    Decal = 3,
};

enum class AlphaCompare : u8 {
    None = 0,
    Threshold = 1,
    Dither = 3,
};

/// The colour a blender cycle weighs: the `P` and `M` inputs.
enum class BlendColor : u8 {
    Pixel = 0,
    Memory = 1,
    Blend = 2,
    Fog = 3,
};

/// The first weight, `A`.
enum class BlendAlpha : u8 {
    PixelAlpha = 0,
    FogAlpha = 1,
    ShadeAlpha = 2,
    Zero = 3,
};

/// The second weight, `B`.
enum class BlendWeight : u8 {
    InverseAlpha = 0,
    MemoryAlpha = 1,
    One = 2,
    Zero = 3,
};

/// One cycle of the blender: `(first * firstWeight + second * secondWeight) /
/// (firstWeight + secondWeight)`.
struct BlenderCycle {
    BlendColor first = BlendColor::Pixel;
    BlendAlpha firstWeight = BlendAlpha::PixelAlpha;
    BlendColor second = BlendColor::Pixel;
    BlendWeight secondWeight = BlendWeight::InverseAlpha;

    friend bool operator==(const BlenderCycle&, const BlenderCycle&) = default;
};

/// The RDP's other mode: the high word `G_SETOTHERMODE_H` writes and the low
/// word `G_SETOTHERMODE_L` writes, with accessors for the fields the renderer
/// reads.
struct OtherMode {
    u32 high = 0;
    u32 low = 0;

    [[nodiscard]] CycleType getCycleType() const;
    [[nodiscard]] bool isTexturePerspective() const;
    [[nodiscard]] bool isTextureLod() const;
    [[nodiscard]] TlutMode getTlutMode() const;
    [[nodiscard]] TextureFilter getTextureFilter() const;
    [[nodiscard]] AlphaCompare getAlphaCompare() const;
    [[nodiscard]] bool isPrimitiveDepth() const;
    [[nodiscard]] bool isAntiAliased() const;
    [[nodiscard]] bool isDepthCompared() const;
    [[nodiscard]] bool isDepthUpdated() const;
    [[nodiscard]] bool isImageRead() const;
    [[nodiscard]] bool isColorOnCoverage() const;
    [[nodiscard]] DepthMode getDepthMode() const;
    [[nodiscard]] bool isCoverageTimesAlpha() const;
    [[nodiscard]] bool isAlphaCoverageSelect() const;
    [[nodiscard]] bool isForceBlend() const;

    /// The blender settings for `cycle`, 0 or 1. One-cycle mode blends with
    /// cycle 0.
    [[nodiscard]] BlenderCycle getBlenderCycle(usize cycle) const;

    friend bool operator==(const OtherMode&, const OtherMode&) = default;
};

} // namespace n64
