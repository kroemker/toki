#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneSurface.h"

#include <span>
#include <vector>

namespace toki::scene {

/// `mesh` with each solid face made of its polygon's material, which every
/// face drafted from it keeps, and every face grown from it takes.
[[nodiscard]] SceneMesh makeSolidMesh(SceneMesh mesh, const SceneSurface& surface,
                                      const Collision& collision);

/// The polygons no face has, as a draft of one more room numbered `room`:
/// every vertex of the collision, and a face for each such polygon at its
/// corners, made of its material, its `original` the polygon's index.
[[nodiscard]] RoomDraft makeSolidOnlyDraft(const Collision& collision, const SceneSurface& surface,
                                           usize room);

/// The collision as `drafts` of rooms, and `solidOnly` of the polygons no
/// face has, leave the surface. Polygons of faces of rooms not drafted, and
/// those no face has when there is no `solidOnly`, are kept as they are.
/// Each solid face of a draft is the polygon of the face it was drafted from,
/// in its place, or a new one after the rest; a face without collision has
/// no polygon.
[[nodiscard]] Collision makeDraftedCollision(const Collision& collision,
                                             const SceneSurface& surface,
                                             std::span<const RoomDraft> drafts,
                                             const RoomDraft* solidOnly);

/// The mesh vertices welded to the collision vertices `moves` moves, moved
/// with them.
[[nodiscard]] std::vector<MeshVertexMove>
findWeldedMeshMoves(const SceneSurface& surface, std::span<const CollisionVertexMove> moves);

/// The collision as moving vertices of `mesh` and of the collision leaves
/// it. A corner of a polygon follows a move of its own vertex, of the mesh
/// vertex at the corner of the polygon's face, or, for a polygon no face has,
/// of a mesh vertex its vertex is welded to. A vertex all of whose corners
/// follow one move is moved; the corners that follow it of one whose corners
/// do not all follow are given a vertex of their own. Polygons that moved are
/// faced again, and the bounds made around every vertex.
[[nodiscard]] Collision makeWeldedCollision(const Collision& collision,
                                            const SceneSurface& surface, const SceneMesh& mesh,
                                            std::span<const MeshVertexMove> meshMoves,
                                            std::span<const CollisionVertexMove> collisionMoves);

} // namespace toki::scene
