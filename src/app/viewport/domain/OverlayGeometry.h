#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <vector>

namespace toki::viewport {

/// How a kind of line is drawn over the frame.
struct LineStyle {
    std::array<u8, 4> color{};
    /// In target pixels.
    f32 width = 1.0F;
};

/// The edges of every triangle `rdpResult` draws through `viewport`, at the
/// clip-space positions the RSP gave its vertices. An edge shared by
/// several triangles is drawn once.
[[nodiscard]] std::vector<gfx::OverlayLine> makeTriangleEdges(const n64::RspResult& rspResult,
                                                              const n64::RdpResult& rdpResult,
                                                              const n64::Viewport& viewport,
                                                              const LineStyle& style);

/// The three edges of each of `triangles`, as a highlight outlines what it
/// fills.
[[nodiscard]] std::vector<gfx::OverlayLine> makeOutlines(const std::vector<ClipTriangle>& triangles,
                                                         const LineStyle& style);

/// How the lines of a `ViewportOverlay` are drawn, in target pixels.
struct ArrowStyle {
    f32 width = 2.0F;
    f32 arrowLength = 11.0F;
    f32 arrowWidth = 5.0F;
    /// How far short of a line's end its arrowhead points, so that what the
    /// line ends at does not cover it.
    f32 arrowGap = 9.0F;
};

/// The lines of `overlay` in the clip space `viewProjection` takes the world
/// to, each with an arrowhead at the ends that have one. An arrowhead lies
/// flat on a screen of `size` pixels, at the depth of the end it points at.
[[nodiscard]] std::vector<gfx::OverlayLine> makeArrowLines(const ViewportOverlay& overlay,
                                                           const glm::mat4& viewProjection,
                                                           FrameSize size, const ArrowStyle& style);

/// A floor grid on the XZ plane, `GridCells` cells each way from the cell
/// under `center`, each `GridSpacing` units, with the X axis red and the Z
/// axis blue where they pass through it.
[[nodiscard]] std::vector<gfx::OverlayLine> makeGridLines(const glm::mat4& viewProjection,
                                                          const glm::vec3& center);

inline constexpr f32 GridSpacing = 100.0F;
inline constexpr s32 GridCells = 30;

/// `line` without the part on the camera's side of the near plane, where `z`
/// is below `-w`; none when all of it is. What is left has a `w` above zero,
/// as the overlay needs to push a line out sideways on screen.
[[nodiscard]] std::optional<gfx::OverlayLine> clipToNearPlane(const gfx::OverlayLine& line);

} // namespace toki::viewport
