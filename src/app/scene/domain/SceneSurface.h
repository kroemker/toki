#pragma once

#include "app/scene/Types.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <compare>
#include <optional>
#include <vector>

namespace toki::scene {

struct Collision;
struct RoomMesh;
struct SceneMesh;

/// A face of a room's mesh: the room's number, and the face's index there.
struct MeshFaceRef {
    usize room = 0;
    usize face = 0;

    friend auto operator<=>(const MeshFaceRef&, const MeshFaceRef&) = default;
};

/// A vertex of a room's mesh: the room's number, and the vertex's index there.
struct MeshVertexRef {
    usize room = 0;
    usize vertex = 0;

    friend auto operator<=>(const MeshVertexRef&, const MeshVertexRef&) = default;
};

/// A layer's rooms' meshes and its collision read as one surface. A face is
/// solid when a polygon has its three corners, and that polygon is the face's;
/// a polygon that is no face's is solid only, drawn by nothing. A collision
/// vertex standing where mesh vertices stand is welded to them, and follows
/// them when they move.
struct SceneSurface {
    /// For each room by its number, for each face of its mesh, its polygon.
    std::vector<std::vector<std::optional<usize>>> facePolygons;
    /// For each polygon of the collision, its face, if it has one.
    std::vector<std::optional<MeshFaceRef>> polygonFaces;
    /// For each vertex of the collision, the mesh vertices it is welded to.
    std::vector<std::vector<MeshVertexRef>> welds;
};

/// Pairs each polygon of `collision` with a face of `mesh` at its three
/// corners, by the whole units both stand at, the first such face no other
/// polygon has; and welds each collision vertex to the mesh vertices at its
/// position.
[[nodiscard]] SceneSurface pairSurface(const SceneMesh& mesh, const Collision& collision);

/// The polygon `face` is, if it is solid.
[[nodiscard]] std::optional<usize> findFacePolygon(const SceneSurface& surface, MeshFaceRef face);

/// The polygons that are no face's, in order.
[[nodiscard]] std::vector<usize> listSolidOnlyPolygons(const SceneSurface& surface);

/// Whether a polygon can be made of face `face` of `room`: its corners, in
/// whole units, are not in a line. A face that cannot is never solid, however
/// it is set, since the collision it would be is left out.
[[nodiscard]] bool canFaceBeSolid(const RoomMesh& room, usize face);

/// A position in the world in whole units, as collision stores it.
[[nodiscard]] std::array<s16, 3> roundToCollision(const glm::vec3& position);

} // namespace toki::scene
