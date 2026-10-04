#pragma once

#include "gfx/BlendSetup.h"
#include "gfx/Types.h"
#include "n64/Rdp.h"

#include <array>

namespace gfx {

/// The vertex shader's uniforms, laid out as `VertexUniforms` in
/// Ubershader.hlsl. A primitive given in screen pixels uses scale `(1, -1, 1)`
/// and no translation; a negative `viewportTranslate[3]` means the depth comes
/// from the vertices rather than the primitive depth.
struct VertexUniforms {
    std::array<f32, 4> viewportScale{};
    std::array<f32, 4> viewportTranslate{};
    std::array<f32, 4> screen{};

    friend bool operator==(const VertexUniforms&, const VertexUniforms&) = default;
};

static_assert(sizeof(VertexUniforms) == 48, "VertexUniforms must match Ubershader.hlsl");

/// The fragment shader's uniforms, laid out as `FragmentUniforms` in
/// Ubershader.hlsl: the RDP state a draw needs, packed into words.
struct FragmentUniforms {
    std::array<u32, 4> combiner{};
    std::array<u32, 4> colors{};
    std::array<u32, 4> constants{};
    std::array<u32, 4> modes{};
    std::array<u32, 4> tile0{};
    std::array<u32, 4> tile1{};

    friend bool operator==(const FragmentUniforms&, const FragmentUniforms&) = default;
};

static_assert(sizeof(FragmentUniforms) == 96, "FragmentUniforms must match Ubershader.hlsl");

/// What a primitive itself says about how it is drawn, beyond the RDP state.
struct PrimitiveInfo {
    bool textured = false;
    bool shade = false;
    bool rectangle = false;
    u8 tile = 0;
};

/// Packs `state` for the shader. `textureMemoryOffset` is where the draw's
/// texture memory starts in the storage buffer, in 32-bit words. An untextured
/// primitive carries no tiles, so that it batches with others regardless of
/// what the tiles hold.
[[nodiscard]] FragmentUniforms makeFragmentUniforms(const n64::RdpState& state,
                                                    const PrimitiveInfo& primitive,
                                                    const BlendSetup& blendSetup,
                                                    u32 textureMemoryOffset);

} // namespace gfx
