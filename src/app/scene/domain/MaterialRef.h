#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/CollisionMaterials.h"

#include <variant>

namespace toki::scene {

/// A material the rooms are drawn with, by its index into the scene's
/// materials.
struct RenderMaterialRef {
    usize material = 0;

    friend bool operator==(const RenderMaterialRef&, const RenderMaterialRef&) = default;
};

/// A material selected in the Scene panel, whose settings the Properties panel
/// shows: one the rooms are drawn with, or one of the shown layer's
/// collision.
using MaterialRef = std::variant<RenderMaterialRef, CollisionMaterial>;

} // namespace toki::scene
