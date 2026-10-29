#include "CollisionGeometry.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionMaterials.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace toki::scene {

namespace {

constexpr f32 NormalOne = 32767.0F;
constexpr f32 Flat = 1.0e-6F;
constexpr u16 VertexIndexMask = 0x1FFF;
constexpr std::array<PolygonFlag, 4> Flags{PolygonFlag::IgnoreCamera, PolygonFlag::IgnoreEntity,
                                           PolygonFlag::IgnoreProjectiles, PolygonFlag::Conveyor};

using Point = std::array<s16, 3>;

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

[[nodiscard]] CollisionPolygon setVertexIndex(CollisionPolygon polygon, const usize corner,
                                              const usize index) {
    polygon.vertexData[corner] = static_cast<u16>((polygon.vertexData[corner] & ~VertexIndexMask) |
                                                  (index & VertexIndexMask));
    return polygon;
}

[[nodiscard]] std::optional<std::array<Point, 3>> findCorners(const Collision& collision,
                                                              const CollisionPolygon& polygon) {
    std::array<Point, 3> corners{};
    for (usize corner = 0; corner < 3; ++corner) {
        const u16 index = getVertexIndex(polygon, corner);
        if (index >= collision.vertices.size()) {
            return std::nullopt;
        }
        corners[corner] = collision.vertices[index];
    }
    return corners;
}

/// The vertices of a collision as polygons are added to it: those it had,
/// then those added at places none of them stands.
class VertexList {
public:
    explicit VertexList(const std::vector<Point>& vertices) : vertices(vertices) {
        for (usize i = 0; i < vertices.size(); ++i) {
            indices.try_emplace(vertices[i], i);
        }
    }

    [[nodiscard]] usize find(const Point& point) {
        const auto [found, isAdded] = indices.try_emplace(point, vertices.size());
        if (isAdded) {
            vertices.push_back(point);
        }
        return found->second;
    }

    [[nodiscard]] std::vector<Point> take() {
        return std::move(vertices);
    }

private:
    std::vector<Point> vertices;
    std::map<Point, usize> indices;
};

} // namespace

std::optional<CollisionPolygon> makeFacedPolygon(CollisionPolygon polygon,
                                                 const std::vector<std::array<s16, 3>>& vertices) {
    std::array<glm::vec3, 3> corners{};
    for (usize corner = 0; corner < 3; ++corner) {
        const u16 index = getVertexIndex(polygon, corner);
        if (index >= vertices.size()) {
            return std::nullopt;
        }
        corners[corner] = glm::vec3(vertices[index][0], vertices[index][1], vertices[index][2]);
    }
    const glm::vec3 facing = glm::cross(corners[1] - corners[0], corners[2] - corners[0]);
    if (glm::length(facing) < Flat) {
        return std::nullopt;
    }
    const glm::vec3 unit = glm::normalize(facing);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        polygon.normal[static_cast<usize>(axis)] = roundToS16(unit[axis] * NormalOne);
    }
    polygon.distance = roundToS16(-glm::dot(unit, corners[0]));
    return polygon;
}

CollisionPolygon makeMaterialPolygon(CollisionPolygon polygon, const CollisionMaterial& material) {
    polygon.type = material.type;
    for (const PolygonFlag flag : Flags) {
        polygon = makeFlaggedPolygon(polygon, flag, hasMaterialFlag(material, flag));
    }
    return polygon;
}

Collision makeBoundedCollision(Collision collision) {
    if (collision.vertices.empty()) {
        return collision;
    }
    collision.minBounds = collision.vertices.front();
    collision.maxBounds = collision.vertices.front();
    for (const Point& vertex : collision.vertices) {
        for (usize axis = 0; axis < 3; ++axis) {
            collision.minBounds[axis] = std::min(collision.minBounds[axis], vertex[axis]);
            collision.maxBounds[axis] = std::max(collision.maxBounds[axis], vertex[axis]);
        }
    }
    return collision;
}

std::vector<SurfacePolygon> listSurfacePolygons(const Collision& collision) {
    std::vector<SurfacePolygon> polygons;
    for (usize i = 0; i < collision.polygons.size(); ++i) {
        const std::optional<std::array<Point, 3>> corners =
            findCorners(collision, collision.polygons[i]);
        polygons.push_back({.corners = corners.value_or(std::array<Point, 3>{}),
                            .material = getPolygonMaterial(collision.polygons[i]),
                            .read = i});
    }
    return polygons;
}

Collision makeSurfaceCollision(const Collision& collision,
                               const std::span<const SurfacePolygon> polygons) {
    VertexList vertices(collision.vertices);
    std::vector<CollisionPolygon> listed;
    std::vector<bool> isKept;
    for (const SurfacePolygon& polygon : polygons) {
        if (polygon.read && *polygon.read < collision.polygons.size()) {
            const CollisionPolygon& read = collision.polygons[*polygon.read];
            if (findCorners(collision, read) == polygon.corners) {
                listed.push_back(makeMaterialPolygon(read, polygon.material));
                isKept.push_back(true);
                continue;
            }
        }
        CollisionPolygon added = makeMaterialPolygon({}, polygon.material);
        for (usize corner = 0; corner < 3; ++corner) {
            added = setVertexIndex(added, corner, vertices.find(polygon.corners[corner]));
        }
        listed.push_back(added);
        isKept.push_back(false);
    }
    const std::vector<Point> all = vertices.take();
    Collision made = collision;
    made.polygons.clear();
    for (usize i = 0; i < listed.size(); ++i) {
        if (isKept[i]) {
            made.polygons.push_back(listed[i]);
        } else if (const std::optional<CollisionPolygon> faced = makeFacedPolygon(listed[i], all)) {
            made.polygons.push_back(*faced);
        }
    }
    std::vector<bool> isUsed(all.size(), true);
    for (const CollisionPolygon& polygon : collision.polygons) {
        for (usize corner = 0; corner < 3; ++corner) {
            if (const u16 index = getVertexIndex(polygon, corner); index < isUsed.size()) {
                isUsed[index] = false;
            }
        }
    }
    for (usize i = collision.vertices.size(); i < all.size(); ++i) {
        isUsed[i] = false;
    }
    for (const CollisionPolygon& polygon : made.polygons) {
        for (usize corner = 0; corner < 3; ++corner) {
            if (const u16 index = getVertexIndex(polygon, corner); index < isUsed.size()) {
                isUsed[index] = true;
            }
        }
    }
    std::vector<usize> moved(all.size(), 0);
    made.vertices.clear();
    for (usize i = 0; i < all.size(); ++i) {
        if (isUsed[i]) {
            moved[i] = made.vertices.size();
            made.vertices.push_back(all[i]);
        }
    }
    for (CollisionPolygon& polygon : made.polygons) {
        for (usize corner = 0; corner < 3; ++corner) {
            if (const u16 index = getVertexIndex(polygon, corner); index < moved.size()) {
                polygon = setVertexIndex(polygon, corner, moved[index]);
            }
        }
    }
    if (made.vertices == collision.vertices) {
        return made;
    }
    return makeBoundedCollision(std::move(made));
}

} // namespace toki::scene
