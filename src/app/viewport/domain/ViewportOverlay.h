#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <optional>
#include <vector>

namespace toki::viewport {

/// A line drawn over the viewport between two points of the world, as from a
/// switch to the door it opens, with an arrowhead at either end that has one.
/// `color` is packed as ImGui packs it, `0xAABBGGRR`.
struct OverlayLine {
    glm::vec3 from{};
    glm::vec3 to{};
    u32 color = 0xFFFFFFFFU;
    bool hasArrowAtFrom = false;
    bool hasArrowAtTo = true;
};

/// What is drawn over the viewport's image, on top of everything.
struct ViewportOverlay {
    std::vector<OverlayLine> lines;
};

/// A line's two ends on the screen, in pixels from the top left, and whether
/// each is the line's own end rather than where the near plane cut it.
struct ScreenSegment {
    glm::vec2 from{};
    glm::vec2 to{};
    bool isFromWhole = true;
    bool isToWhole = true;
};

/// Where `from` to `to` falls on a screen of `size` pixels seen through
/// `viewProjection`, cut where it passes behind the camera, or nothing when all
/// of it is behind.
[[nodiscard]] std::optional<ScreenSegment> projectSegment(const glm::mat4& viewProjection,
                                                          const glm::vec3& from,
                                                          const glm::vec3& to, glm::vec2 size);

} // namespace toki::viewport
