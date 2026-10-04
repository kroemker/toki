#include "OtherMode.h"

#include "n64/TileDescriptor.h"
#include "n64/Types.h"

namespace n64 {

namespace {

constexpr u32 TexturePerspective = 1U << 19;
constexpr u32 TextureLod = 1U << 16;
constexpr u32 PrimitiveDepth = 1U << 2;
constexpr u32 AntiAlias = 1U << 3;
constexpr u32 DepthCompare = 1U << 4;
constexpr u32 DepthUpdate = 1U << 5;
constexpr u32 ImageRead = 1U << 6;
constexpr u32 ColorOnCoverage = 1U << 7;
constexpr u32 CoverageTimesAlpha = 1U << 12;
constexpr u32 AlphaCoverageSelect = 1U << 13;
constexpr u32 ForceBlend = 1U << 14;

} // namespace

CycleType OtherMode::getCycleType() const {
    return static_cast<CycleType>((high >> 20) & 0x03);
}

bool OtherMode::isTexturePerspective() const {
    return (high & TexturePerspective) != 0;
}

bool OtherMode::isTextureLod() const {
    return (high & TextureLod) != 0;
}

TlutMode OtherMode::getTlutMode() const {
    const u32 mode = (high >> 14) & 0x03;
    return mode >= 2 ? static_cast<TlutMode>(mode) : TlutMode::None;
}

TextureFilter OtherMode::getTextureFilter() const {
    const u32 filter = (high >> 12) & 0x03;
    return filter == 0 ? TextureFilter::Point : static_cast<TextureFilter>(filter | 2);
}

AlphaCompare OtherMode::getAlphaCompare() const {
    const u32 compare = low & 0x03;
    return (compare & 1) == 0 ? AlphaCompare::None : static_cast<AlphaCompare>(compare);
}

bool OtherMode::isPrimitiveDepth() const {
    return (low & PrimitiveDepth) != 0;
}

bool OtherMode::isAntiAliased() const {
    return (low & AntiAlias) != 0;
}

bool OtherMode::isDepthCompared() const {
    return (low & DepthCompare) != 0;
}

bool OtherMode::isDepthUpdated() const {
    return (low & DepthUpdate) != 0;
}

bool OtherMode::isImageRead() const {
    return (low & ImageRead) != 0;
}

bool OtherMode::isColorOnCoverage() const {
    return (low & ColorOnCoverage) != 0;
}

DepthMode OtherMode::getDepthMode() const {
    return static_cast<DepthMode>((low >> 10) & 0x03);
}

bool OtherMode::isCoverageTimesAlpha() const {
    return (low & CoverageTimesAlpha) != 0;
}

bool OtherMode::isAlphaCoverageSelect() const {
    return (low & AlphaCoverageSelect) != 0;
}

bool OtherMode::isForceBlend() const {
    return (low & ForceBlend) != 0;
}

BlenderCycle OtherMode::getBlenderCycle(const usize cycle) const {
    const u32 shift = cycle == 0 ? 2 : 0;
    return BlenderCycle{
        .first = static_cast<BlendColor>((low >> (28 + shift)) & 0x03),
        .firstWeight = static_cast<BlendAlpha>((low >> (24 + shift)) & 0x03),
        .second = static_cast<BlendColor>((low >> (20 + shift)) & 0x03),
        .secondWeight = static_cast<BlendWeight>((low >> (16 + shift)) & 0x03),
    };
}

} // namespace n64
