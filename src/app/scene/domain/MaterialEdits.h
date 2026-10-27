#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MeshOperations.h"
#include "base/Diagnostic.h"

#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace toki::scene {

class SceneModel;
struct SceneMaterial;
struct SceneMesh;

/// The rooms `picks` names with their picked faces drawn under `material`:
/// each face moved into the run of triangles that draws the material in its
/// room. A room that does not draw the material is an error naming it, since
/// a material is not yet copied between rooms.
[[nodiscard]] std::expected<std::vector<OperatedRoom>, base::Diagnostic>
assignMaterial(const SceneMesh& mesh, std::span<const MeshPick> picks,
               const SceneMaterial& material);

/// Renames the list of its own that sets `material` in every room to the
/// fast64 name of `name`, and every pointer at it with it.
[[nodiscard]] std::expected<void, base::Diagnostic>
renameMaterial(SceneModel& model, const SceneMaterial& material, std::string_view name);

/// Copies the list of its own that sets `material` in every room as a
/// material named `name`, drawn by no face yet.
[[nodiscard]] std::expected<void, base::Diagnostic>
duplicateMaterial(SceneModel& model, const SceneMaterial& material, std::string_view name);

} // namespace toki::scene
