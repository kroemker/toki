#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/RoomMesh.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// A corner of a draft's face: the vertex it is at, and the `Vtx` it is
/// loaded from, whole. Its position is the vertex's once that moved.
struct DraftCorner {
    usize vertex = 0;
    std::array<u8, 16> vtx{};

    friend bool operator==(const DraftCorner&, const DraftCorner&) = default;
};

/// A face of a draft, and the run of vertex loads and triangles that draws it:
/// the run `draw` is a command of, or, with no list, a new run drawn under
/// `material`, the `Gfx` array its RDP state is set by.
struct DraftFace {
    std::array<DraftCorner, 3> corners{};
    MeshDraw draw;
    std::string material;
    /// What takes the positions its corners store to the world.
    glm::mat4 transform{1.0F};
    bool isLit = false;
    bool isTranslucent = false;
    usize part = 0;
    /// The face of the room's mesh it was made from, if any. A face is
    /// changed when it has none, or when its corners differ from that one's.
    std::optional<usize> original;
};

struct DraftVertex {
    glm::vec3 position{0.0F};
    /// Whether it was put somewhere else, so that its corners store its
    /// position in place of the one they were loaded with.
    bool isMoved = false;
};

/// A room's mesh as an edit of its faces leaves it, before it is written:
/// vertices as the mesh numbers them, and new ones after, and faces in the
/// order they are drawn within each run.
struct RoomDraft {
    usize room = 0;
    std::vector<DraftVertex> vertices;
    std::vector<DraftFace> faces;
};

/// `mesh` as a draft that changes nothing.
[[nodiscard]] RoomDraft makeRoomDraft(const RoomMesh& mesh);

/// Where corner `corner` of `face` stands in the world.
[[nodiscard]] glm::vec3 findCornerPosition(const RoomDraft& draft, const DraftFace& face,
                                           usize corner);

/// The `Vtx` corner `corner` of `face` is written as, its position stored
/// through the inverse of the face's transform when its vertex moved.
[[nodiscard]] std::array<u8, 16> makeCornerVtx(const RoomDraft& draft, const DraftFace& face,
                                               usize corner);

/// Puts `vtx`'s position at `world`, stored through the inverse of
/// `transform`.
void setVtxPosition(std::array<u8, 16>& vtx, const glm::mat4& transform, const glm::vec3& world);

/// The texture coordinates and colour of a corner at `point` on `face`,
/// projected onto the face's plane and read off its corners as the face
/// maps them: what a corner a face is cut or grown at takes.
[[nodiscard]] std::array<u8, 16> makeProjectedVtx(const RoomDraft& draft, const DraftFace& face,
                                                  const glm::vec3& point);

/// The vertices of `draft` that no face uses are left where they are, so
/// that numbers stay; these are the ones a face uses.
[[nodiscard]] std::vector<bool> findUsedVertices(const RoomDraft& draft);

/// Adds a vertex at `position`, moved so that it is written there.
usize addDraftVertex(RoomDraft& draft, const glm::vec3& position);

} // namespace toki::scene
