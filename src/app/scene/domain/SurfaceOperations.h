#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MaterialBindings.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
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
/// not solid without one. Faces that cannot be solid are left as they are.
[[nodiscard]] std::vector<OperatedRoom>
setFacesCollision(const SceneMesh& mesh, std::span<const MeshPick> picks,
                  const std::optional<CollisionMaterial>& material);

/// The rooms of `mesh` with the faces `picks` names solid: those that were
/// as they were, each other that can be made of what its material's faces are
/// made of.
[[nodiscard]] std::vector<OperatedRoom>
makeFacesSolid(const SceneMesh& mesh, const Collision& collision, std::span<const MeshPick> picks);

/// `solidOnly`, the draft of the polygons no face has, with the faces `faces`
/// names made of `material`, or taken out without one.
[[nodiscard]] OperatedRoom setSolidOnlyCollision(RoomDraft solidOnly, std::span<const usize> faces,
                                                 const std::optional<CollisionMaterial>& material);

/// The rooms of `mesh` with the solid faces `picks` names drawn no more, and
/// `solidOnly`, the draft of the polygons no face has, with a face for each
/// of them, made of what it was. Faces that are not solid are left alone.
[[nodiscard]] std::vector<OperatedRoom>
undrawFaces(const SceneMesh& mesh, std::span<const MeshPick> picks, RoomDraft solidOnly);

/// A render material's binding as it stands against a layer's collision and
/// mesh: the collision material it makes faces of, or why it makes them of
/// none the collision has, and how many of its faces are not made of it.
struct ResolvedBinding {
    MaterialBinding binding;
    std::expected<std::optional<CollisionMaterial>, std::string> collision;
    usize unbound = 0;
};

/// `operated` with the faces each names made of what `binding` makes faces
/// of: what faces newly drawn under a bound material are made. Without a
/// binding, or with one to collision that is not there, it is unchanged.
[[nodiscard]] std::vector<OperatedRoom>
makeBoundFaces(std::vector<OperatedRoom> operated, const std::optional<ResolvedBinding>& binding);

/// The binding of each of `materials`, where it has one.
[[nodiscard]] std::vector<std::optional<ResolvedBinding>>
resolveMaterialBindings(const MaterialBindings& bindings, const Collision& collision,
                        const SceneMesh& mesh, const std::vector<SceneMaterial>& materials);

/// Whether the faces drawn under `materials[material]` are made of more than
/// one collision material, or some are solid and some not, of those that can
/// be solid.
[[nodiscard]] bool isCollisionMixed(const SceneMesh& mesh,
                                    const std::vector<SceneMaterial>& materials, usize material);

/// How many faces drawn under `materials[material]` are not made of `bound`,
/// or are solid when it is nothing; a face that cannot be solid is as bound.
[[nodiscard]] usize countUnbound(const SceneMesh& mesh, const std::vector<SceneMaterial>& materials,
                                 usize material, const std::optional<CollisionMaterial>& bound);

/// The rooms of `mesh` that draw a face under `materials[material]` that is
/// not made of `bound`, with every such face made of it, or not solid when
/// it is nothing; a face that cannot be solid is left as it is.
[[nodiscard]] std::vector<OperatedRoom> bindFaces(const SceneMesh& mesh,
                                                  const std::vector<SceneMaterial>& materials,
                                                  usize material,
                                                  const std::optional<CollisionMaterial>& bound);

} // namespace toki::scene
