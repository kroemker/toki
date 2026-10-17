#pragma once

#include "gfx/DrawBatches.h"
#include "gfx/Overlay.h"
#include "gfx/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <span>
#include <vector>

namespace gfx {

/// One corner of a line's quad as Overlay.hlsl reads it: its own end, the
/// other end, and which side of the line and how far it is pushed out.
struct GpuOverlayVertex {
    std::array<f32, 4> position{};
    std::array<f32, 4> other{};
    /// The side, -1 or 1, and the line's width in target pixels.
    std::array<f32, 2> extrusion{};
    std::array<u8, 4> color{};
};

static_assert(sizeof(GpuOverlayVertex) == 44,
              "GpuOverlayVertex must match the overlay pipeline's vertex attributes");

/// The overlay's vertex uniforms, laid out as `OverlayUniforms` in
/// Overlay.hlsl: the viewport as the ubershader reads it, the target's size,
/// how far a line is pulled toward the camera, and how opaque it is drawn.
struct OverlayUniforms {
    std::array<f32, 4> viewportScale{};
    std::array<f32, 4> viewportTranslate{};
    std::array<f32, 4> screen{};
};

static_assert(sizeof(OverlayUniforms) == 48, "OverlayUniforms must match Overlay.hlsl");

/// How far a line is pulled toward the camera, in the depth target's units,
/// so that it wins against the triangle it lies on.
inline constexpr f32 OverlayDepthBias = 1.0F / 2048.0F;

/// Six vertices for each line, two triangles that the vertex stage pushes
/// out sideways to the line's width on screen.
[[nodiscard]] std::vector<GpuOverlayVertex> makeOverlayVertices(std::span<const OverlayLine> lines);

[[nodiscard]] OverlayUniforms makeOverlayUniforms(const n64::Viewport& viewport, ScreenSize screen,
                                                  f32 opacity);

} // namespace gfx
