#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace toki::scene {

struct SceneMaterial;

/// What a render material makes the faces drawn under it: solid, of a
/// surface type, by its words, and the flags of what passes through, or not
/// solid. Words rather than an index, so that a binding means the same in
/// every collision of the scene.
struct MaterialBinding {
    bool isSolid = false;
    SurfaceType surface;
    u8 flags = 0;

    friend bool operator==(const MaterialBinding&, const MaterialBinding&) = default;
};

/// The render materials of a scene whose faces' collision follows them, each
/// by `makeMaterialKey`. A material with no binding leaves its faces as they
/// are.
struct MaterialBindings {
    std::map<std::string, MaterialBinding, std::less<>> materials;

    friend bool operator==(const MaterialBindings&, const MaterialBindings&) = default;
};

/// What a binding names `material` by: its name, where every room sets it by
/// a list of its own, else its RDP state.
[[nodiscard]] std::string makeMaterialKey(const SceneMaterial& material);

[[nodiscard]] std::optional<MaterialBinding> findMaterialBinding(const MaterialBindings& bindings,
                                                                 const SceneMaterial& material);

/// A binding to `material` of `collision`, or to not being solid without one.
[[nodiscard]] MaterialBinding makeMaterialBinding(const Collision& collision,
                                                  const std::optional<CollisionMaterial>& material);

/// The collision material of `collision` that `binding` makes faces of, or
/// nothing for one that makes them not solid; an error when `collision` has
/// no surface type of its words.
[[nodiscard]] std::expected<std::optional<CollisionMaterial>, std::string>
findBoundCollision(const Collision& collision, const MaterialBinding& binding);

/// `bindings` with the binding of the material named by key `from`, if any,
/// moved to key `to`: what renaming the material makes of it.
[[nodiscard]] MaterialBindings makeRenamedBindings(MaterialBindings bindings, std::string_view from,
                                                   std::string_view to);

/// `bindings` with every binding to `from` bound to `to` instead, of those
/// with `flags` alone when given: what a surface type's settings being
/// changed makes of them.
[[nodiscard]] MaterialBindings makeReboundSurface(MaterialBindings bindings,
                                                  const SurfaceType& from, const SurfaceType& to,
                                                  std::optional<u8> flags);

/// `bindings` with every binding to `surface` with flags `from` bound to it
/// with flags `to` instead: what a collision material's flags being changed
/// makes of them.
[[nodiscard]] MaterialBindings makeReboundFlags(MaterialBindings bindings,
                                                const SurfaceType& surface, u8 from, u8 to);

} // namespace toki::scene
