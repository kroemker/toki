#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/SceneSelection.h"

#include <string_view>

namespace toki::scene {

struct Scene;
struct SceneLayer;

/// The Properties panel for a polygon or a waterbox of a layer's collision
/// selected: what the game reads of it, decoded, and its words as they are.
/// Nothing of it is edited.
class CollisionForm {
public:
    /// Names the values of surface types' settings as `names` does.
    void setNames(const CollisionNames* names) {
        collisionNames = names;
    }

    /// `selected` is how many nodes are selected, of which this is the last.
    void drawPolygon(const Scene& scene, const CollisionPolygonRef& where, usize selected) const;

    void drawWaterBox(const Scene& scene, const WaterBoxRef& where, usize selected) const;

private:
    void drawSurfaceSection(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                            u16 type) const;
    void drawSurfaceRows(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                         const SurfaceFields& fields) const;
    void drawNamedRow(std::string_view label, std::string_view help, SurfaceSetting setting,
                      s64 value) const;

    const CollisionNames* collisionNames = nullptr;
};

} // namespace toki::scene
