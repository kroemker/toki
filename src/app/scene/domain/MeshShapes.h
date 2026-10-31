#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>

#include <expected>
#include <span>
#include <vector>

namespace toki::scene {

struct SceneMaterial;
struct SceneMesh;

enum class MeshShape : u8 {
    Box,
    Plane,
};

/// How wide a shape added to a room is.
inline constexpr f32 ShapeSize = 100.0F;

/// The faces of `shape`, `size` wide, standing on `center`, faced outward,
/// and drawn as `like` is: in its run, under its transform and material, with
/// its colour. Texture coordinates are laid on each face's plane, a texel to
/// a unit, from the shape's corner. Returns the faces added.
std::vector<usize> addMeshShape(RoomDraft& draft, MeshShape shape, const glm::vec3& center,
                                f32 size, DraftFace like);

/// The rooms `picks` names with their picked faces taken out, and the room
/// numbered `room` with them added, each drawn by the run that draws its
/// material there, its texture coordinates and colour kept. A face whose
/// material the room does not draw is an error naming it.
[[nodiscard]] std::expected<std::vector<OperatedRoom>, base::Diagnostic>
moveFacesToRoom(const SceneMesh& mesh, std::span<const MeshPick> picks, usize room,
                const std::vector<SceneMaterial>& materials);

} // namespace toki::scene
