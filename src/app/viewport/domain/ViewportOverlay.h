#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/vector_float3.hpp>

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

/// What the content's owner draws over the viewport's image, hidden by what
/// stands in front of it.
struct ViewportOverlay {
    std::vector<OverlayLine> lines;
};

} // namespace toki::viewport
