#pragma once

#include "gfx/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <vector>

namespace gfx {

/// A line drawn over the frame between two points in the N64's clip space,
/// where the RSP puts the vertices it transforms, so that a line along a
/// triangle's edge has the triangle's own depth. `width` is in target pixels.
struct OverlayLine {
    std::array<f32, 4> from{};
    std::array<f32, 4> to{};
    std::array<u8, 4> color{};
    f32 width = 1.0F;
    /// How opaque the part the frame hides is drawn; at zero it is not drawn.
    f32 hiddenOpacity = 0.0F;

    friend bool operator==(const OverlayLine&, const OverlayLine&) = default;
};

/// A filled triangle drawn over the frame, its corners in the N64's clip space
/// as an `OverlayLine`'s ends are.
struct OverlayTriangle {
    std::array<std::array<f32, 4>, 3> corners{};
    std::array<u8, 4> color{};
    /// How opaque the part the frame hides is drawn; at zero it is not drawn.
    f32 hiddenOpacity = 0.0F;
};

/// What is drawn over the frame, in the render pass that drew it and tested
/// against its depth. Every point has a `w` above zero: a line that crossed
/// the near plane was clipped to it before.
struct Overlay {
    n64::Viewport viewport = n64::makeDefaultViewport();
    /// Drawn before the lines, so that a line along a triangle's edge shows.
    std::vector<OverlayTriangle> triangles;
    std::vector<OverlayLine> lines;
};

} // namespace gfx
