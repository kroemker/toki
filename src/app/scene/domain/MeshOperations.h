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
    Extrude,
    Inset,
    Subdivide,
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

/// Grows the region the faces `faces` names out of the rest: the faces get
/// vertices of their own, and every edge the region shares with no other of
/// its faces a side of two faces from where it stood to where the region's
/// faces now are. Nothing moves; the region is moved after. Returns the
/// region's faces.
std::vector<usize> extrudeFaces(RoomDraft& draft, std::span<const usize> faces);

/// How far toward its middle a face is shrunk by an inset.
inline constexpr f32 InsetFraction = 0.25F;

/// Shrinks each face `faces` names toward its middle and joins it to where it
/// was by a ring of faces, their texture coordinates and colours projected
/// from the face. Returns the shrunk faces.
std::vector<usize> insetFaces(RoomDraft& draft, std::span<const usize> faces);

/// Cuts each edge of the faces `faces` names in two, and each face that holds
/// a cut edge, picked or not, into the triangles its cuts make, so that no
/// crack opens. Returns the faces the picked ones were cut into.
std::vector<usize> subdivideFaces(RoomDraft& draft, std::span<const usize> faces);

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
