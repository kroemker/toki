#include "SelectionShape.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float4.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace toki::viewport {

namespace {

/// Calls `visit` with every triangle drawn for the draws owned by `owner`.
template <typename Visit>
void visitOwnedTriangles(const n64::RspResult& result, const DrawnOwners& drawn, const usize owner,
                         const Visit& visit) {
    for (usize draw = 0; draw < drawn.ranges.size() && draw < drawn.owners.size(); ++draw) {
        if (!drawn.ranges[draw] || drawn.owners[draw] != owner) {
            continue;
        }
        const OutputRange& range = *drawn.ranges[draw];
        for (usize i = range.first; i < range.end && i < result.outputs.size(); ++i) {
            if (const auto* triangle = std::get_if<n64::Triangle>(&result.outputs[i])) {
                visit(*triangle);
            }
        }
    }
}

} // namespace

std::vector<ClipTriangle> listOwnedTriangles(const n64::RspResult& result, const DrawnOwners& drawn,
                                             const std::span<const usize> selected) {
    std::vector<ClipTriangle> triangles;
    for (const usize owner : selected) {
        visitOwnedTriangles(result, drawn, owner, [&triangles](const n64::Triangle& triangle) {
            triangles.push_back({triangle.vertices[0].position, triangle.vertices[1].position,
                                 triangle.vertices[2].position});
        });
    }
    return triangles;
}

std::vector<std::optional<WorldBox>> findOwnedBoxes(const n64::RspResult& result,
                                                    const DrawnOwners& drawn,
                                                    const std::span<const usize> selected,
                                                    const glm::mat4& inverseViewProjection) {
    std::vector<std::optional<WorldBox>> boxes;
    for (const usize owner : selected) {
        std::optional<WorldBox>& box = boxes.emplace_back();
        visitOwnedTriangles(result, drawn, owner, [&](const n64::Triangle& triangle) {
            for (const n64::ShadedVertex& vertex : triangle.vertices) {
                const std::array<f32, 4>& clip = vertex.position;
                if (clip[3] <= 0.0F) {
                    continue;
                }
                const glm::vec4 world =
                    inverseViewProjection * glm::vec4(clip[0], clip[1], clip[2], clip[3]);
                const glm::vec3 point = glm::vec3(world) / world.w;
                if (!box) {
                    box = WorldBox{.lowest = point, .highest = point};
                } else {
                    box->lowest = glm::min(box->lowest, point);
                    box->highest = glm::max(box->highest, point);
                }
            }
        });
    }
    return boxes;
}

std::array<std::array<glm::vec3, 2>, 12> listBoxEdges(const WorldBox& box) {
    const auto corner = [&box](const usize bits) {
        return glm::vec3((bits & 1U) != 0 ? box.highest.x : box.lowest.x,
                         (bits & 2U) != 0 ? box.highest.y : box.lowest.y,
                         (bits & 4U) != 0 ? box.highest.z : box.lowest.z);
    };
    std::array<std::array<glm::vec3, 2>, 12> edges{};
    usize edge = 0;
    for (usize bits = 0; bits < 8; ++bits) {
        for (const usize axis : {1U, 2U, 4U}) {
            if ((bits & axis) == 0) {
                edges[edge++] = {corner(bits), corner(bits | axis)};
            }
        }
    }
    return edges;
}

} // namespace toki::viewport
