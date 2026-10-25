#include "OverlayGeometry.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace toki::viewport {

namespace {

using Position = std::array<f32, 4>;

/// Keeps the endpoints of a clipped line off the plane itself, where a `w`
/// of zero would leave its screen position undefined.
constexpr f32 NearMargin = 1.0e-4F;

[[nodiscard]] f32 findNearDistance(const Position& position) {
    return position[2] + position[3];
}

[[nodiscard]] Position interpolate(const Position& from, const Position& to, const f32 t) {
    Position point{};
    for (usize i = 0; i < point.size(); ++i) {
        point[i] = from[i] + ((to[i] - from[i]) * t);
    }
    return point;
}

[[nodiscard]] std::array<Position, 2> orderEnds(const Position& a, const Position& b) {
    return std::ranges::lexicographical_compare(a, b) ? std::array{a, b} : std::array{b, a};
}

constexpr std::array<u8, 4> GridColor{190, 194, 204, 56};
constexpr std::array<u8, 4> XAxisColor{226, 84, 84, 170};
constexpr std::array<u8, 4> ZAxisColor{84, 132, 236, 170};

[[nodiscard]] std::array<u8, 4> unpackColor(const u32 color) {
    return {static_cast<u8>(color & 0xFFU), static_cast<u8>((color >> 8) & 0xFFU),
            static_cast<u8>((color >> 16) & 0xFFU), static_cast<u8>(color >> 24)};
}

[[nodiscard]] Position makeClip(const glm::mat4& viewProjection, const glm::vec3& point) {
    const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0F);
    return {clip.x, clip.y, clip.z, clip.w};
}

/// A point on the screen in pixels from its centre, y up, as `clip` falls on
/// one of `size`.
[[nodiscard]] glm::vec2 findScreenPoint(const Position& clip, const glm::vec2& halfSize) {
    return glm::vec2(clip[0] / clip[3], clip[1] / clip[3]) * halfSize;
}

[[nodiscard]] Position placeAtDepth(const glm::vec2& screen, const glm::vec2& halfSize,
                                    const Position& depth) {
    const glm::vec2 device = screen / halfSize;
    return {device.x * depth[3], device.y * depth[3], depth[2], depth[3]};
}

void addArrowHead(std::vector<gfx::OverlayLine>& lines, const gfx::OverlayLine& line,
                  const glm::vec2& halfSize, const ArrowStyle& style) {
    const glm::vec2 tail = findScreenPoint(line.from, halfSize);
    const glm::vec2 head = findScreenPoint(line.to, halfSize);
    const f32 length = glm::length(head - tail);
    if (length < style.arrowLength + style.arrowGap) {
        return;
    }
    const glm::vec2 direction = (head - tail) / length;
    const glm::vec2 tip = head - (direction * style.arrowGap);
    const glm::vec2 base = tip - (direction * style.arrowLength);
    const glm::vec2 side = glm::vec2(-direction.y, direction.x) * style.arrowWidth;
    const Position tipClip = placeAtDepth(tip, halfSize, line.to);
    for (const glm::vec2& wing : {base + side, base - side}) {
        lines.push_back({.from = placeAtDepth(wing, halfSize, line.to),
                         .to = tipClip,
                         .color = line.color,
                         .width = line.width});
    }
}

/// How far from flat four corners may be and still count as one face's.
constexpr f32 FlatTolerance = 1.0e-4F;

/// Whether `a`, `b`, `c` and `d` are corners of one plane. Clip space is the
/// world through a linear map, so they are when the matrix of the four is
/// singular.
[[nodiscard]] bool isFlat(const Position& a, const Position& b, const Position& c,
                          const Position& d) {
    const glm::mat4 corners{glm::vec4{a[0], a[1], a[2], a[3]}, glm::vec4{b[0], b[1], b[2], b[3]},
                            glm::vec4{c[0], c[1], c[2], c[3]}, glm::vec4{d[0], d[1], d[2], d[3]}};
    const f32 scale = glm::length(corners[0]) * glm::length(corners[1]) * glm::length(corners[2]) *
                      glm::length(corners[3]);
    return std::abs(glm::determinant(corners)) <= FlatTolerance * scale;
}

[[nodiscard]] std::array<u8, 4> findGridColor(const f32 coordinate, const std::array<u8, 4>& axis) {
    return coordinate == 0.0F ? axis : GridColor;
}

} // namespace

std::vector<gfx::OverlayLine> makeOutlines(const std::vector<ClipTriangle>& triangles,
                                           const LineStyle& style) {
    std::vector<gfx::OverlayLine> lines;
    lines.reserve(triangles.size() * 3);
    for (const ClipTriangle& triangle : triangles) {
        for (usize i = 0; i < 3; ++i) {
            if (const std::optional<gfx::OverlayLine> clipped =
                    clipToNearPlane({.from = triangle[i],
                                     .to = triangle[(i + 1) % 3],
                                     .color = style.color,
                                     .width = style.width})) {
                lines.push_back(*clipped);
            }
        }
    }
    return lines;
}

std::vector<gfx::OverlayLine> makeFaceEdges(const std::vector<ClipTriangle>& triangles,
                                            const LineStyle& style) {
    struct Edge {
        Position firstOpposite{};
        usize count = 0;
        bool isInside = false;
    };

    std::map<std::array<Position, 2>, Edge> edges;
    for (const ClipTriangle& triangle : triangles) {
        for (usize i = 0; i < 3; ++i) {
            const std::array<Position, 2> ends = orderEnds(triangle[i], triangle[(i + 1) % 3]);
            const Position& opposite = triangle[(i + 2) % 3];
            Edge& edge = edges[ends];
            if (edge.count == 0) {
                edge.firstOpposite = opposite;
            }
            ++edge.count;
            edge.isInside =
                edge.count == 2 && isFlat(ends[0], ends[1], edge.firstOpposite, opposite);
        }
    }
    std::vector<gfx::OverlayLine> lines;
    for (const auto& [ends, edge] : edges) {
        if (edge.isInside) {
            continue;
        }
        if (const std::optional<gfx::OverlayLine> clipped = clipToNearPlane(
                {.from = ends[0], .to = ends[1], .color = style.color, .width = style.width})) {
            lines.push_back(*clipped);
        }
    }
    return lines;
}

std::vector<gfx::OverlayLine> makeArrowLines(const ViewportOverlay& overlay,
                                             const glm::mat4& viewProjection, const FrameSize size,
                                             const ArrowStyle& style) {
    const glm::vec2 halfSize(static_cast<f32>(size.width) / 2.0F,
                             static_cast<f32>(size.height) / 2.0F);
    std::vector<gfx::OverlayLine> lines;
    for (const OverlayLine& line : overlay.lines) {
        const gfx::OverlayLine whole{.from = makeClip(viewProjection, line.from),
                                     .to = makeClip(viewProjection, line.to),
                                     .color = unpackColor(line.color),
                                     .width = style.width};
        const std::optional<gfx::OverlayLine> clipped = clipToNearPlane(whole);
        if (!clipped) {
            continue;
        }
        lines.push_back(*clipped);
        if (line.hasArrowAtTo && clipped->to == whole.to) {
            addArrowHead(lines, *clipped, halfSize, style);
        }
        if (line.hasArrowAtFrom && clipped->from == whole.from) {
            addArrowHead(lines,
                         {.from = clipped->to,
                          .to = clipped->from,
                          .color = clipped->color,
                          .width = clipped->width},
                         halfSize, style);
        }
    }
    return lines;
}

std::vector<gfx::OverlayLine> makeGridLines(const glm::mat4& viewProjection,
                                            const glm::vec3& center) {
    const f32 extent = GridSpacing * static_cast<f32>(GridCells);
    const glm::vec2 middle(std::round(center.x / GridSpacing) * GridSpacing,
                           std::round(center.z / GridSpacing) * GridSpacing);
    std::vector<gfx::OverlayLine> lines;
    for (s32 cell = -GridCells; cell <= GridCells; ++cell) {
        const f32 offset = static_cast<f32>(cell) * GridSpacing;
        const f32 x = middle.x + offset;
        const f32 z = middle.y + offset;
        const std::array<gfx::OverlayLine, 2> pair{{
            {.from = makeClip(viewProjection, {x, 0.0F, middle.y - extent}),
             .to = makeClip(viewProjection, {x, 0.0F, middle.y + extent}),
             .color = findGridColor(x, ZAxisColor)},
            {.from = makeClip(viewProjection, {middle.x - extent, 0.0F, z}),
             .to = makeClip(viewProjection, {middle.x + extent, 0.0F, z}),
             .color = findGridColor(z, XAxisColor)},
        }};
        for (const gfx::OverlayLine& line : pair) {
            if (const std::optional<gfx::OverlayLine> clipped = clipToNearPlane(line)) {
                lines.push_back(*clipped);
            }
        }
    }
    return lines;
}

std::vector<gfx::OverlayLine> makeTriangleEdges(const n64::RspResult& rspResult,
                                                const n64::RdpResult& rdpResult,
                                                const n64::Viewport& viewport,
                                                const LineStyle& style) {
    std::vector<std::array<Position, 2>> edges;
    for (const n64::Draw& draw : rdpResult.draws) {
        const auto* triangle = std::get_if<n64::Triangle>(&draw.primitive);
        if (triangle == nullptr || triangle->viewport >= rspResult.viewports.size() ||
            rspResult.viewports[triangle->viewport] != viewport) {
            continue;
        }
        for (usize i = 0; i < 3; ++i) {
            edges.push_back(orderEnds(triangle->vertices[i].position,
                                      triangle->vertices[(i + 1) % 3].position));
        }
    }
    std::ranges::sort(edges);
    const auto [first, last] = std::ranges::unique(edges);
    edges.erase(first, last);

    std::vector<gfx::OverlayLine> lines;
    lines.reserve(edges.size());
    for (const auto& [from, to] : edges) {
        if (const std::optional<gfx::OverlayLine> clipped = clipToNearPlane(
                {.from = from, .to = to, .color = style.color, .width = style.width})) {
            lines.push_back(*clipped);
        }
    }
    return lines;
}

std::optional<gfx::OverlayLine> clipToNearPlane(const gfx::OverlayLine& line) {
    const f32 fromDistance = findNearDistance(line.from) - NearMargin;
    const f32 toDistance = findNearDistance(line.to) - NearMargin;
    if (fromDistance < 0.0F && toDistance < 0.0F) {
        return std::nullopt;
    }
    gfx::OverlayLine clipped = line;
    if (fromDistance < 0.0F) {
        clipped.from = interpolate(line.from, line.to, fromDistance / (fromDistance - toDistance));
    } else if (toDistance < 0.0F) {
        clipped.to = interpolate(line.from, line.to, fromDistance / (fromDistance - toDistance));
    }
    return clipped;
}

} // namespace toki::viewport
