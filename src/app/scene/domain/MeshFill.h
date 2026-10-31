#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MeshDraft.h"

#include <glm/ext/vector_float3.hpp>

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// Makes faces between `vertices`, as Blender's Make Face does with vertices
/// no edge joins: they are taken around their middle, seen along the plane
/// they lie nearest, and the polygon they make is cut into triangles. Each
/// triangle stands at its own corners, so vertices off one plane bend it.
/// Faced the way a face joined to them at an edge runs, else toward
/// `viewer`, and drawn as a face at one of them is, solid if that is. Returns
/// the faces made, or why none can be: fewer than three vertices, all in a
/// line, or none drawn by a face.
[[nodiscard]] std::expected<std::vector<usize>, std::string>
fillVertices(RoomDraft& draft, std::span<const usize> vertices, const glm::vec3& viewer);

} // namespace toki::scene
