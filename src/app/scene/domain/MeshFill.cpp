#include "MeshFill.h"

#include "app/scene/domain/MeshDraft.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

/// How small, against the square of the longest distance between them, the
/// area of points may be before they are taken as in a line.
constexpr f32 InLineFraction = 1e-4F;

struct FillPlane {
    glm::vec3 middle{0.0F};
    glm::vec3 normal{0.0F};
    glm::vec3 across{0.0F};
    glm::vec3 up{0.0F};
};

/// The plane through the two of `points` farthest apart and the one farthest
/// from the line between them, if they are not all in a line.
[[nodiscard]] std::optional<FillPlane> findFillPlane(const std::span<const glm::vec3> points) {
    FillPlane plane;
    for (const glm::vec3& point : points) {
        plane.middle += point;
    }
    plane.middle /= static_cast<f32>(points.size());
    std::array<usize, 2> farthest{0, 0};
    f32 longest = 0.0F;
    for (usize first = 0; first < points.size(); ++first) {
        for (usize second = first + 1; second < points.size(); ++second) {
            const f32 distance = glm::length(points[second] - points[first]);
            if (distance > longest) {
                longest = distance;
                farthest = {first, second};
            }
        }
    }
    const glm::vec3 along = points[farthest[1]] - points[farthest[0]];
    glm::vec3 widest(0.0F);
    for (const glm::vec3& point : points) {
        const glm::vec3 normal = glm::cross(along, point - points[farthest[0]]);
        if (glm::length(normal) > glm::length(widest)) {
            widest = normal;
        }
    }
    if (longest == 0.0F || glm::length(widest) <= InLineFraction * longest * longest) {
        return std::nullopt;
    }
    plane.normal = glm::normalize(widest);
    plane.across = glm::normalize(along);
    plane.up = glm::cross(plane.normal, plane.across);
    return plane;
}

[[nodiscard]] f32 findTurn(const glm::vec2& from, const glm::vec2& by, const glm::vec2& to) {
    const glm::vec2 first = by - from;
    const glm::vec2 second = to - from;
    return (first.x * second.y) - (first.y * second.x);
}

[[nodiscard]] bool isInside(const glm::vec2& point, const std::array<glm::vec2, 3>& triangle) {
    return findTurn(triangle[0], triangle[1], point) >= 0.0F &&
           findTurn(triangle[1], triangle[2], point) >= 0.0F &&
           findTurn(triangle[2], triangle[0], point) >= 0.0F;
}

/// The triangles the polygon `points` runs counterclockwise around is cut
/// into, by clipping ears, as indices of `points`. Corners in a line with
/// their neighbours make no triangle of their own.
[[nodiscard]] std::vector<std::array<usize, 3>> clipEars(const std::vector<glm::vec2>& points,
                                                         const f32 smallest) {
    std::vector<usize> left(points.size());
    for (usize index = 0; index < left.size(); ++index) {
        left[index] = index;
    }
    std::vector<std::array<usize, 3>> triangles;
    while (left.size() > 3) {
        bool isClipped = false;
        for (usize at = 0; at < left.size() && !isClipped; ++at) {
            const usize previous = left[(at + left.size() - 1) % left.size()];
            const usize corner = left[at];
            const usize next = left[(at + 1) % left.size()];
            const std::array<glm::vec2, 3> ear{points[previous], points[corner], points[next]};
            if (findTurn(ear[0], ear[1], ear[2]) <= smallest) {
                continue;
            }
            const bool isEmpty = std::ranges::none_of(left, [&](const usize other) {
                return other != previous && other != corner && other != next &&
                       isInside(points[other], ear);
            });
            if (isEmpty) {
                triangles.push_back({previous, corner, next});
                left.erase(left.begin() + static_cast<std::ptrdiff_t>(at));
                isClipped = true;
            }
        }
        if (!isClipped) {
            break;
        }
    }
    for (usize at = 1; at + 1 < left.size(); ++at) {
        if (findTurn(points[left[0]], points[left[at]], points[left[at + 1]]) > smallest) {
            triangles.push_back({left[0], left[at], left[at + 1]});
        }
    }
    return triangles;
}

/// The face of `draft` that runs from `from` to `to` along one of its edges.
[[nodiscard]] std::optional<usize> findEdgeFace(const RoomDraft& draft, const usize from,
                                                const usize to) {
    for (usize face = 0; face < draft.faces.size(); ++face) {
        const std::array<DraftCorner, 3>& corners = draft.faces[face].corners;
        for (usize corner = 0; corner < 3; ++corner) {
            if (corners[corner].vertex == from && corners[(corner + 1) % 3].vertex == to) {
                return face;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<usize> findVertexFace(const RoomDraft& draft,
                                                  const std::set<usize>& vertices) {
    for (usize face = 0; face < draft.faces.size(); ++face) {
        if (std::ranges::any_of(draft.faces[face].corners, [&vertices](const DraftCorner& corner) {
                return vertices.contains(corner.vertex);
            })) {
            return face;
        }
    }
    return std::nullopt;
}

} // namespace

std::expected<std::vector<usize>, std::string>
fillVertices(RoomDraft& draft, const std::span<const usize> vertices, const glm::vec3& viewer) {
    const std::set<usize> unique(vertices.begin(), vertices.end());
    if (unique.size() < 3) {
        return std::unexpected("Pick three vertices or more.");
    }
    std::vector<usize> order(unique.begin(), unique.end());
    std::vector<glm::vec3> points;
    for (const usize vertex : order) {
        points.push_back(draft.vertices[vertex].position);
    }
    const std::optional<FillPlane> plane = findFillPlane(points);
    if (!plane) {
        return std::unexpected("The picked vertices are in a line.");
    }
    std::vector<f32> angles;
    for (const glm::vec3& point : points) {
        const glm::vec3 offset = point - plane->middle;
        angles.push_back(std::atan2(glm::dot(offset, plane->up), glm::dot(offset, plane->across)));
    }
    std::vector<usize> sorted(order.size());
    for (usize index = 0; index < sorted.size(); ++index) {
        sorted[index] = index;
    }
    std::ranges::sort(sorted, [&angles](const usize first, const usize second) {
        return angles[first] < angles[second];
    });
    std::vector<glm::vec2> flat;
    f32 longest = 0.0F;
    for (const usize index : sorted) {
        const glm::vec3 offset = points[index] - plane->middle;
        flat.emplace_back(glm::dot(offset, plane->across), glm::dot(offset, plane->up));
        longest = std::max(longest, glm::length(offset));
    }

    std::optional<usize> like;
    std::optional<bool> isReversed;
    for (usize at = 0; at < sorted.size() && !isReversed; ++at) {
        const usize from = order[sorted[at]];
        const usize to = order[sorted[(at + 1) % sorted.size()]];
        if (const std::optional<usize> same = findEdgeFace(draft, from, to)) {
            like = same;
            isReversed = true;
        } else if (const std::optional<usize> other = findEdgeFace(draft, to, from)) {
            like = other;
            isReversed = false;
        }
    }
    if (!isReversed) {
        isReversed = glm::dot(plane->normal, viewer - plane->middle) < 0.0F;
    }
    if (!like) {
        like = findVertexFace(draft, unique);
    }
    if (!like) {
        return std::unexpected("No face is drawn at the picked vertices to draw the new one as.");
    }

    const DraftFace source = draft.faces[*like];
    std::vector<usize> made;
    for (std::array<usize, 3> triangle : clipEars(flat, InLineFraction * longest * longest)) {
        if (*isReversed) {
            std::swap(triangle[1], triangle[2]);
        }
        DraftFace face = source;
        face.original.reset();
        for (usize corner = 0; corner < 3; ++corner) {
            const usize vertex = order[sorted[triangle[corner]]];
            const glm::vec3& position = draft.vertices[vertex].position;
            std::array<u8, 16> vtx = makeProjectedVtx(draft, source, position);
            for (const DraftCorner& kept : source.corners) {
                if (kept.vertex == vertex) {
                    vtx = kept.vtx;
                }
            }
            setVtxPosition(vtx, source.transform, position);
            face.corners[corner] = DraftCorner{.vertex = vertex, .vtx = vtx};
        }
        made.push_back(draft.faces.size());
        draft.faces.push_back(face);
    }
    if (made.empty()) {
        return std::unexpected("The picked vertices are in a line.");
    }
    return made;
}

} // namespace toki::scene
