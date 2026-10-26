#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
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

    friend bool operator==(const OverlayLine&, const OverlayLine&) = default;
};

/// A dot drawn over the viewport at a point of the world, as a vertex is
/// marked while vertices are selected. `size` is in points across.
struct OverlayPoint {
    glm::vec3 position{};
    u32 color = 0xFFFFFFFFU;
    f32 size = 5.0F;

    friend bool operator==(const OverlayPoint&, const OverlayPoint&) = default;
};

/// A plain line of what is edited, as an edge of a mesh, `width` in points.
struct OverlayEdge {
    glm::vec3 from{};
    glm::vec3 to{};
    u32 color = 0xFFFFFFFFU;
    f32 width = 1.0F;

    friend bool operator==(const OverlayEdge&, const OverlayEdge&) = default;
};

/// A filled triangle of what is edited, as a selected face.
struct OverlayFace {
    std::array<glm::vec3, 3> corners{};
    u32 color = 0xFFFFFFFFU;

    friend bool operator==(const OverlayFace&, const OverlayFace&) = default;
};

/// What the content's owner draws over the viewport's image, hidden by what
/// stands in front of it: the lines between what it links, shown as the Flag
/// Links overlay, and the edges and vertices of what is edited by faces,
/// edges or vertices, shown whenever overlays are.
struct ViewportOverlay {
    std::vector<OverlayLine> lines;
    std::vector<OverlayFace> faces;
    std::vector<OverlayEdge> edges;
    std::vector<OverlayPoint> points;

    friend bool operator==(const ViewportOverlay&, const ViewportOverlay&) = default;
};

} // namespace toki::viewport
