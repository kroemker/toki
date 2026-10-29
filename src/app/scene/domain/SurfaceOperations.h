#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"

#include <optional>
#include <span>
#include <vector>

namespace toki::scene {

struct Collision;

/// What a face drawn under `material` is made solid with: what most solid
/// faces drawn under it are made of, else what most polygons are made of,
/// else the first surface type without flags.
[[nodiscard]] CollisionMaterial findDefaultCollision(const SceneMesh& mesh,
                                                     const Collision& collision,
                                                     const MeshMaterial& material);

/// The rooms of `mesh` with the faces `picks` names made of `material`, or
/// not solid without one.
[[nodiscard]] std::vector<OperatedRoom> setFacesCollision(const SceneMesh& mesh,
                                                          std::span<const MeshPick> picks,
                                                          const std::optional<CollisionMaterial>& material);

/// The rooms of `mesh` with the faces `picks` names solid: those that were
/// as they were, each other made of what its material's faces are made of.
[[nodiscard]] std::vector<OperatedRoom>
makeFacesSolid(const SceneMesh& mesh, const Collision& collision, std::span<const MeshPick> picks);

/// `solidOnly`, the draft of the polygons no face has, with the faces `faces`
/// names made of `material`, or taken out without one.
[[nodiscard]] OperatedRoom setSolidOnlyCollision(RoomDraft solidOnly, std::span<const usize> faces,
                                                 const std::optional<CollisionMaterial>& material);

/// The rooms of `mesh` with the solid faces `picks` names drawn no more, and
/// `solidOnly`, the draft of the polygons no face has, with a face for each
/// of them, made of what it was. Faces that are not solid are left alone.
[[nodiscard]] std::vector<OperatedRoom> undrawFaces(const SceneMesh& mesh,
                                                    std::span<const MeshPick> picks,
                                                    RoomDraft solidOnly);

} // namespace toki::scene
