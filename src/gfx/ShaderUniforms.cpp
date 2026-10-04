#include "ShaderUniforms.h"

#include "gfx/BlendSetup.h"
#include "gfx/Types.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/TextureMemory.h"
#include "n64/TileDescriptor.h"

#include <array>

namespace gfx {

namespace {

constexpr u32 FlagTextured = 1U << 0;
constexpr u32 FlagShade = 1U << 1;
constexpr u32 FlagTwoCycle = 1U << 2;
constexpr u32 FlagCopy = 1U << 3;
constexpr u32 FlagFill = 1U << 4;
constexpr u32 FlagAlphaCompare = 1U << 5;
constexpr u32 FlagAlphaCompareDither = 1U << 6;
constexpr u32 FlagAlphaCoverageSelect = 1U << 7;
constexpr u32 FlagCoverageTimesAlpha = 1U << 8;
constexpr u32 FlagPerspective = 1U << 9;
constexpr u32 FlagForceBlend = 1U << 10;
constexpr u32 FlagColorImage32 = 1U << 11;
constexpr u32 TlutShift = 12;
constexpr u32 FilterShift = 14;

u32 packColor(const n64::Color& color) {
    return (u32{color[0]} << 24) | (u32{color[1]} << 16) | (u32{color[2]} << 8) | color[3];
}

u32 packBytes(const std::array<u8, 3>& bytes) {
    return u32{bytes[0]} | (u32{bytes[1]} << 8) | (u32{bytes[2]} << 16);
}

u32 packSlots(const std::array<n64::CombinerInput, 4>& slots) {
    u32 packed = 0;
    for (usize i = 0; i < slots.size(); ++i) {
        packed |= u32{static_cast<u8>(slots[i])} << (i * 8);
    }
    return packed;
}

u32 packAxis(const n64::TileAxis& axis) {
    return (axis.clamp ? 1U : 0U) | (axis.mirror ? 2U : 0U) | (u32{axis.mask} << 2) |
           (u32{axis.shift} << 6);
}

std::array<u32, 4> packTile(const n64::TileDescriptor& tile) {
    return {
        u32{static_cast<u8>(tile.format)} | (u32{static_cast<u8>(tile.size)} << 3) |
            (u32{tile.palette} << 5) | (packAxis(tile.axes[0]) << 9) |
            (packAxis(tile.axes[1]) << 19),
        u32{tile.line} | (u32{tile.address} << 16),
        u32{tile.axes[0].low} | (u32{tile.axes[0].high} << 16),
        u32{tile.axes[1].low} | (u32{tile.axes[1].high} << 16),
    };
}

u32 packBlenderCycle(const n64::BlenderCycle& cycle) {
    return (u32{static_cast<u8>(cycle.first)} << 6) |
           (u32{static_cast<u8>(cycle.firstWeight)} << 4) |
           (u32{static_cast<u8>(cycle.second)} << 2) | static_cast<u8>(cycle.secondWeight);
}

u32 makeFlags(const n64::RdpState& state, const PrimitiveInfo& primitive) {
    const n64::OtherMode& otherMode = state.otherMode;
    const n64::CycleType cycleType = otherMode.getCycleType();
    const bool copy = cycleType == n64::CycleType::Copy;
    u32 flags = 0;
    flags |= primitive.textured ? FlagTextured : 0;
    flags |= primitive.shade ? FlagShade : 0;
    flags |= cycleType == n64::CycleType::Two ? FlagTwoCycle : 0;
    flags |= copy ? FlagCopy : 0;
    flags |= cycleType == n64::CycleType::Fill ? FlagFill : 0;
    flags |= otherMode.getAlphaCompare() != n64::AlphaCompare::None ? FlagAlphaCompare : 0;
    flags |= otherMode.getAlphaCompare() == n64::AlphaCompare::Dither ? FlagAlphaCompareDither : 0;
    flags |= otherMode.isAlphaCoverageSelect() ? FlagAlphaCoverageSelect : 0;
    flags |= otherMode.isCoverageTimesAlpha() ? FlagCoverageTimesAlpha : 0;
    flags |= otherMode.isTexturePerspective() || primitive.rectangle ? FlagPerspective : 0;
    flags |= otherMode.isForceBlend() ? FlagForceBlend : 0;
    flags |= state.colorImageSize == n64::TexelSize::Bits32 ? FlagColorImage32 : 0;
    flags |= u32{static_cast<u8>(otherMode.getTlutMode())} << TlutShift;
    const n64::TextureFilter filter =
        copy ? n64::TextureFilter::Point : otherMode.getTextureFilter();
    flags |= u32{static_cast<u8>(filter)} << FilterShift;
    return flags;
}

} // namespace

FragmentUniforms makeFragmentUniforms(const n64::RdpState& state, const PrimitiveInfo& primitive,
                                      const BlendSetup& blendSetup, const u32 textureMemoryOffset) {
    const n64::OtherMode& otherMode = state.otherMode;
    const usize finalCycle = otherMode.getCycleType() == n64::CycleType::Two ? 1 : 0;
    const u32 blending = packBlenderCycle(otherMode.getBlenderCycle(0)) |
                         (packBlenderCycle(otherMode.getBlenderCycle(finalCycle)) << 8) |
                         (u32{static_cast<u8>(blendSetup.path)} << 16);
    return FragmentUniforms{
        .combiner = {packSlots(state.combineMode.cycles[0].color),
                     packSlots(state.combineMode.cycles[0].alpha),
                     packSlots(state.combineMode.cycles[1].color),
                     packSlots(state.combineMode.cycles[1].alpha)},
        .colors = {packColor(state.primitive), packColor(state.environment), packColor(state.blend),
                   packColor(state.fog)},
        .constants = {state.primitiveLodFraction, packBytes(state.keyCenter),
                      packBytes(state.keyScale),
                      u32{state.convert[4]} | (u32{state.convert[5]} << 16)},
        .modes = {makeFlags(state, primitive), textureMemoryOffset, state.fill, blending},
        .tile0 = primitive.textured ? packTile(state.tiles[primitive.tile % n64::TileCount])
                                    : std::array<u32, 4>{},
        .tile1 = primitive.textured ? packTile(state.tiles[(primitive.tile + 1U) % n64::TileCount])
                                    : std::array<u32, 4>{},
    };
}

} // namespace gfx
