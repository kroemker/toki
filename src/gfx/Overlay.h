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

    friend bool operator==(const OverlayLine&, const OverlayLine&) = default;
};

/// What is drawn over the frame, in the render pass that drew it and tested
/// against its depth. Every point has a `w` above zero: a line that crossed
/// the near plane was clipped to it before.
struct Overlay {
    n64::Viewport viewport = n64::makeDefaultViewport();
    std::vector<OverlayLine> lines;
};

} // namespace gfx
