#pragma once

#include "app/viewport/Types.h"

namespace toki::viewport {

/// What each surface shows: the frame as the RDP draws it, or one input of
/// the combiner on its own.
enum class Shading : u8 {
    N64,
    ShadeOnly,
    TextureOnly,
    Normals,
    Depth,
};

inline constexpr usize ShadingCount = static_cast<usize>(Shading::Depth) + 1;

/// How textures are filtered: as the other mode says, or every texel a square.
enum class TextureFilter : u8 {
    N64,
    Point,
};

inline constexpr usize TextureFilterCount = static_cast<usize>(TextureFilter::Point) + 1;

/// How the viewport draws a frame. The default is the frame the game would
/// draw; every other choice changes the RSP's task or the RDP's state on the
/// CPU, never the shader.
struct RenderMode {
    Shading shading = Shading::N64;
    bool lighting = true;
    bool fog = true;
    bool culling = true;
    bool alphaCutout = true;
    bool translucency = true;
    TextureFilter textureFilter = TextureFilter::N64;

    friend bool operator==(const RenderMode&, const RenderMode&) = default;
};

} // namespace toki::viewport
