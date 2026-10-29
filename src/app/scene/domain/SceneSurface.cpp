#include "SceneSurface.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/RoomMesh.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace toki::scene {

namespace {

using Point = std::array<s16, 3>;
using Corners = std::array<Point, 3>;

[[nodiscard]] Corners sortCorners(Corners corners) {
    std::ranges::sort(corners);
    return corners;
}

} // namespace

std::array<s16, 3> roundToCollision(const glm::vec3& position) {
    std::array<s16, 3> rounded{};
    for (usize axis = 0; axis < 3; ++axis) {
        rounded[axis] = static_cast<s16>(
            std::clamp(std::lround(position[static_cast<glm::length_t>(axis)]), long{INT16_MIN},
                       long{INT16_MAX}));
    }
    return rounded;
}

SceneSurface pairSurface(const SceneMesh& mesh, const Collision& collision) {
    SceneSurface surface;
    std::map<Corners, std::vector<MeshFaceRef>> facesAt;
    std::map<Point, std::vector<MeshVertexRef>> verticesAt;
    for (const RoomMesh& room : mesh.rooms) {
        if (surface.facePolygons.size() <= room.room) {
            surface.facePolygons.resize(room.room + 1);
        }
        surface.facePolygons[room.room].assign(room.faces.size(), std::nullopt);
        for (usize vertex = 0; vertex < room.vertices.size(); ++vertex) {
            verticesAt[roundToCollision(room.vertices[vertex].position)].push_back(
                {.room = room.room, .vertex = vertex});
        }
        for (usize face = 0; face < room.faces.size(); ++face) {
            Corners corners{};
            for (usize corner = 0; corner < 3; ++corner) {
                corners[corner] =
                    roundToCollision(room.vertices[room.faces[face].vertices[corner]].position);
            }
            facesAt[sortCorners(corners)].push_back({.room = room.room, .face = face});
        }
    }
    surface.polygonFaces.assign(collision.polygons.size(), std::nullopt);
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        Corners corners{};
        bool isWhole = true;
        for (usize corner = 0; corner < 3; ++corner) {
            const u16 vertex = getVertexIndex(collision.polygons[polygon], corner);
            isWhole = isWhole && vertex < collision.vertices.size();
            corners[corner] = isWhole ? collision.vertices[vertex] : Point{};
        }
        const auto found = isWhole ? facesAt.find(sortCorners(corners)) : facesAt.end();
        if (found == facesAt.end()) {
            continue;
        }
        for (const MeshFaceRef& face : found->second) {
            std::optional<usize>& paired = surface.facePolygons[face.room][face.face];
            if (!paired) {
                paired = polygon;
                surface.polygonFaces[polygon] = face;
                break;
            }
        }
    }
    surface.welds.resize(collision.vertices.size());
    for (usize vertex = 0; vertex < collision.vertices.size(); ++vertex) {
        if (const auto found = verticesAt.find(collision.vertices[vertex]);
            found != verticesAt.end()) {
            surface.welds[vertex] = found->second;
        }
    }
    return surface;
}

std::optional<usize> findFacePolygon(const SceneSurface& surface, const MeshFaceRef face) {
    if (face.room >= surface.facePolygons.size() ||
        face.face >= surface.facePolygons[face.room].size()) {
        return std::nullopt;
    }
    return surface.facePolygons[face.room][face.face];
}

std::vector<usize> listSolidOnlyPolygons(const SceneSurface& surface) {
    std::vector<usize> polygons;
    for (usize polygon = 0; polygon < surface.polygonFaces.size(); ++polygon) {
        if (!surface.polygonFaces[polygon]) {
            polygons.push_back(polygon);
        }
    }
    return polygons;
}

} // namespace toki::scene
