#pragma once

#include "app/viewport/Types.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

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

/// `line` without the part on the camera's side of the near plane, where `z`
/// is below `-w`; none when all of it is. What is left has a `w` above zero,
/// as the overlay needs to push a line out sideways on screen.
[[nodiscard]] std::optional<gfx::OverlayLine> clipToNearPlane(const gfx::OverlayLine& line);

} // namespace toki::viewport
