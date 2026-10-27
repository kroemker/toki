#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MeshDraft.h"

#include <optional>
#include <span>
#include <vector>

namespace toki::scene {

struct SceneMesh;

/// What the faces and vertices picked of a room's mesh are made into.
enum class MeshOperation : u8 {
    Delete,
    Flip,
    Merge,
    Split,
};

/// What of a room's mesh an operation is done to: the faces what is picked
/// wholly holds, the faces that hold anything picked, and its vertices.
struct MeshPick {
    usize room = 0;
    std::vector<usize> faces;
    std::vector<usize> touchedFaces;
    std::vector<usize> vertices;
};

/// A room as an operation left it, and what of it to pick afterwards: faces
/// and vertices of the draft.
struct OperatedRoom {
    RoomDraft draft;
    std::vector<usize> faces;
    std::vector<usize> vertices;
};

/// Does `operation` to what `picks` names of `mesh`'s rooms, a draft for
/// each room it changes.
[[nodiscard]] std::vector<OperatedRoom> operateMesh(const SceneMesh& mesh, MeshOperation operation,
                                                    std::span<const MeshPick> picks);

/// Takes the faces `faces` names out of `draft`. Vertices no face uses any
/// more stay numbered, and are not written.
void deleteFaces(RoomDraft& draft, std::span<const usize> faces);

/// Turns each face `faces` names around, its corners in the other order, so
/// that it faces the other way and is culled from the other side.
void flipFaces(RoomDraft& draft, std::span<const usize> faces);

/// Puts `vertices` together at their middle as the first of them. A face
/// left with two corners at one vertex is taken out. Nothing changes with
/// fewer than two. Returns the vertex they are now, if any.
std::optional<usize> mergeVertices(RoomDraft& draft, std::span<const usize> vertices);

/// Splits the faces `faces` names off the rest: each vertex one of them
/// shares with a face not among them is two vertices afterwards, at one
/// place, so that they move apart and are lit apart.
void splitFaces(RoomDraft& draft, std::span<const usize> faces);

} // namespace toki::scene
