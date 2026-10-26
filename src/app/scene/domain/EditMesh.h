#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/RoomMesh.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <compare>
#include <optional>
#include <vector>

namespace toki::scene {

struct Collision;

/// What faces, edges and vertices are picked of: the rooms' meshes, or the
/// layer's collision.
enum class EditTarget : u8 {
    Mesh,
    Collision,
};

/// One piece of what is edited by faces, edges and vertices: a room's mesh,
/// or the collision, its vertices where they stand in the world.
struct EditPiece {
    std::vector<glm::vec3> vertices;
    std::vector<std::array<usize, 3>> faces;
    std::vector<MeshEdge> edges;
};

/// What is edited by faces, edges and vertices, a piece per room of a mesh
/// and one of a collision.
struct EditMesh {
    EditTarget target = EditTarget::Mesh;
    std::vector<EditPiece> pieces;
};

/// A face, edge or vertex of an `EditMesh`: its piece, and its index there.
struct ComponentRef {
    usize piece = 0;
    usize index = 0;

    friend auto operator<=>(const ComponentRef&, const ComponentRef&) = default;
};

/// The rooms of `mesh` as pieces, piece `i` room `i`, for `roomCount` rooms.
[[nodiscard]] EditMesh makeMeshEdit(const SceneMesh& mesh, usize roomCount);

/// `collision` as one piece. A polygon naming a vertex the collision does
/// not hold is left out.
[[nodiscard]] EditMesh makeCollisionEdit(const Collision& collision);

/// The face nearest `origin` along `direction` that a ray from there meets,
/// either side, of the pieces `isShown` holds true, if the ray meets one.
[[nodiscard]] std::optional<ComponentRef> findHitFace(const EditMesh& mesh, const glm::vec3& origin,
                                                      const glm::vec3& direction,
                                                      const std::vector<bool>& isShown);

/// The middle of a face.
[[nodiscard]] glm::vec3 findFaceMiddle(const EditPiece& piece, usize face);

} // namespace toki::scene
