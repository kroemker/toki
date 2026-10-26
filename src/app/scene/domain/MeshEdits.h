#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/RoomMesh.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

class SceneModel;
struct Collision;
struct Scene;

/// Bytes an edit writes over a symbol's own, at `offset` into it. Nothing an
/// address is patched into is written this way.
struct SymbolWrite {
    std::string symbol;
    usize offset = 0;
    std::vector<u8> bytes;

    friend bool operator==(const SymbolWrite&, const SymbolWrite&) = default;
};

/// A vertex of a room's mesh, put at `position` in the world.
struct MeshVertexMove {
    usize room = 0;
    usize vertex = 0;
    glm::vec3 position{0.0F};
};

/// A vertex of a collision, put at `position`.
struct CollisionVertexMove {
    usize vertex = 0;
    std::array<s16, 3> position{};
};

/// What moving vertices of `mesh` writes: each `Vtx` of each vertex at its
/// new place, stored through the inverse of what took it to the world; the
/// normal of each lit `Vtx` of a vertex whose faces changed, as the faces
/// around the vertex now face; and the bounding sphere of each part of a
/// cullable room whose faces changed, around every vertex of the part.
[[nodiscard]] std::vector<SymbolWrite> makeMeshMoveWrites(const Scene& scene, const SceneMesh& mesh,
                                                          std::span<const MeshVertexMove> moves);

/// What moving vertices of `collision`, the collision of `scene`'s images,
/// writes: each vertex, the normal and distance of each polygon of a moved
/// vertex, and the bounds of the collision header around every vertex.
[[nodiscard]] std::vector<SymbolWrite>
makeCollisionMoveWrites(const Scene& scene, const Collision& collision,
                        std::span<const CollisionVertexMove> moves);

/// The collision vertices that stand where a mesh vertex of `moves` stood in
/// `mesh`, moved with it: what a collision linked with the mesh follows.
[[nodiscard]] std::vector<CollisionVertexMove>
findLinkedCollisionMoves(const Collision& collision, const SceneMesh& mesh,
                         std::span<const MeshVertexMove> moves);

/// The mesh vertices that stand where a collision vertex of `moves` stood,
/// moved with it: what a mesh linked with the collision follows.
[[nodiscard]] std::vector<MeshVertexMove>
findLinkedMeshMoves(const Collision& collision, const SceneMesh& mesh,
                    std::span<const CollisionVertexMove> moves);

/// `collision` with `moves` made, the normals and distances of polygons of
/// moved vertices made again, and its bounds around every vertex.
[[nodiscard]] Collision makeMovedCollision(const Collision& collision,
                                           std::span<const CollisionVertexMove> moves);

/// Writes `writes` into the symbols of `model`.
[[nodiscard]] std::expected<void, base::Diagnostic>
applySymbolWrites(SceneModel& model, std::span<const SymbolWrite> writes);

/// `scene`'s images with `writes` made in copies of those they change, as a
/// preview shows an edit before it is made.
[[nodiscard]] std::vector<std::shared_ptr<const z64::MemoryImage>>
makeWrittenImages(const Scene& scene, std::span<const SymbolWrite> writes);

/// Whether `scene`'s images hold every one of `writes`, as once an edit that
/// wrote them was made.
[[nodiscard]] bool hasSymbolWrites(const Scene& scene, std::span<const SymbolWrite> writes);

/// `mesh` with `moves` made, as reading the images the moves were written
/// to would read it again: each `Vtx` stored rounded to whole units.
[[nodiscard]] SceneMesh makeMovedMesh(const SceneMesh& mesh, std::span<const MeshVertexMove> moves);

} // namespace toki::scene
