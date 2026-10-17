#include "OverlayGeometry.h"

#include "app/viewport/Types.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <algorithm>
#include <array>
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

} // namespace

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
