#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/CollisionEdits.h"
#include "base/Diagnostic.h"

#include <compare>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

class SceneModel;
struct Collision;
struct CollisionNames;
struct CollisionPolygon;

/// What a polygon is made of: which of the collision's surface types it has,
/// and its flags, a bit for each `PolygonFlag` by its value.
struct CollisionMaterial {
    u16 type = 0;
    u8 flags = 0;

    friend auto operator<=>(const CollisionMaterial&, const CollisionMaterial&) = default;
};

/// A collision material and the polygons made of it.
struct SceneCollisionMaterial {
    CollisionMaterial material;
    std::vector<usize> polygons;
};

[[nodiscard]] CollisionMaterial getPolygonMaterial(const CollisionPolygon& polygon);

[[nodiscard]] bool hasMaterialFlag(const CollisionMaterial& material, PolygonFlag flag);

[[nodiscard]] CollisionMaterial setMaterialFlag(CollisionMaterial material, PolygonFlag flag,
                                                bool isSet);

/// The materials of `collision`'s polygons, by surface type and then flags,
/// and a material without flags for each surface type no polygon has.
[[nodiscard]] std::vector<SceneCollisionMaterial>
listCollisionMaterials(const Collision& collision);

/// A collision material as a list shows it: its surface type's label, then
/// each of its flags.
[[nodiscard]] std::string makeCollisionMaterialName(const CollisionNames* names,
                                                    const Collision& collision,
                                                    const CollisionMaterial& material);

/// Makes each of `polygons` of the collision of `layer`, the game's scene
/// layer, of `material`: its surface type, and its flags.
[[nodiscard]] std::expected<void, base::Diagnostic>
setPolygonMaterial(SceneModel& model, usize layer, std::span<const usize> polygons,
                   const CollisionMaterial& material);

/// Gives `material` of `collision` the settings `surface`. Every polygon of
/// the material changes, and no other: when polygons of another material
/// share its surface type, the material is given a surface type of its own.
/// Returns the material as it is afterwards.
[[nodiscard]] std::expected<CollisionMaterial, base::Diagnostic>
setMaterialSurface(SceneModel& model, usize layer, const Collision& collision,
                   const CollisionMaterial& material, const SurfaceType& surface);

/// Gives every polygon of `material` of `collision` the flags `flags`, and
/// the bindings to it follow. Returns the material as it is afterwards, which
/// is one with any polygons that already had them.
[[nodiscard]] std::expected<CollisionMaterial, base::Diagnostic>
setMaterialFlags(SceneModel& model, usize layer, const Collision& collision,
                 const CollisionMaterial& material, u8 flags);

} // namespace toki::scene
