#pragma once

#include "app/scene/Types.h"
#include "base/Diagnostic.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <compare>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

struct Scene;

/// One `Vtx` of a scene: the array it is in, and its index there.
struct MeshEntryKey {
    std::string symbol;
    usize index = 0;

    friend auto operator<=>(const MeshEntryKey&, const MeshEntryKey&) = default;
};

/// A `Vtx` a room's triangles use, where the scene's images hold it, and what
/// takes its stored position to the world: the modelview it was loaded under.
struct MeshEntry {
    MeshEntryKey key;
    /// Index into `Scene::images`.
    usize image = 0;
    std::array<s16, 3> stored{};
    /// The whole `Vtx`, as the image holds it.
    std::array<u8, 16> vtx{};
    glm::mat4 transform{1.0F};
    /// Whether a lit triangle uses it, so that its colour holds a normal.
    bool isLit = false;
};

/// A vertex of a room's mesh: every entry that stands for it, at one place.
/// Entries are one vertex when they are at one place and corners of faces
/// that meet along an edge, so a seam where the texture coordinates or the
/// colours change is one vertex, and pieces that only touch are not.
struct MeshVertex {
    glm::vec3 position{0.0F};
    std::vector<usize> entries;
};

/// The triangle command that drew a face: the `Gfx` array it is in, and its
/// index there, counted in commands.
struct MeshDraw {
    std::string list;
    usize command = 0;

    friend auto operator<=>(const MeshDraw&, const MeshDraw&) = default;
};

/// What a face is drawn under: the RDP state it is drawn with, as one number
/// that two faces drawn alike share in any room, the list that set it when
/// that is one of its own (fast64's `mat_` lists), and the symbol of the
/// texture its tile was loaded from, if it is textured.
struct MeshMaterial {
    u64 content = 0;
    std::string list;
    std::string texture;

    friend bool operator==(const MeshMaterial&, const MeshMaterial&) = default;
};

/// A triangle the room draws: its vertices, the entry each corner was loaded
/// from, the part of the room shape and the pass that drew it, the command
/// that drew it, and what it was drawn under.
struct MeshFace {
    std::array<usize, 3> vertices{};
    std::array<usize, 3> corners{};
    usize part = 0;
    bool isTranslucent = false;
    /// Whether it was lit, so that its corners' colours hold normals.
    bool isLit = false;
    MeshDraw draw;
    MeshMaterial material;
};

/// An edge of the mesh, its vertices in increasing order.
struct MeshEdge {
    std::array<usize, 2> vertices{};

    friend auto operator<=>(const MeshEdge&, const MeshEdge&) = default;
};

/// What a room draws, as faces over vertices, read out of the display lists
/// of its room shape by running them.
struct RoomMesh {
    usize room = 0;
    std::vector<MeshEntry> entries;
    std::vector<MeshVertex> vertices;
    std::vector<MeshFace> faces;
    std::vector<MeshEdge> edges;
};

/// The mesh of every room of a layer.
struct SceneMesh {
    usize layer = 0;
    std::vector<RoomMesh> rooms;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the mesh every room of the layer numbered `layer` draws, by running
/// the display lists of its room shape as the viewport does, seen from
/// nowhere, so every triangle comes out in the world with the `Vtx` each of its
/// corners was loaded from. A triangle whose corners are not in a `Vtx` array
/// of the scene's images is left out, and that is a diagnostic.
///
/// With `kept`, entries it has are grouped into vertices as it grouped them,
/// so a vertex moved onto another stays its own until merged; only entries it
/// does not have are grouped by where they are.
[[nodiscard]] SceneMesh readSceneMesh(const Scene& scene, usize layer,
                                      const SceneMesh* kept = nullptr);

/// The mesh of room `room` in `mesh`, if it has one.
[[nodiscard]] const RoomMesh* findRoomMesh(const SceneMesh& mesh, usize room);

/// The middle of a face.
[[nodiscard]] glm::vec3 findFaceCenter(const RoomMesh& mesh, const MeshFace& face);

} // namespace toki::scene
