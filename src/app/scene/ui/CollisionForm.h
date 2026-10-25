#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneSelection.h"

namespace toki::scene {

struct Scene;

/// The Properties panel for a polygon or a waterbox of a layer's collision
/// selected: what the game reads of it, decoded, and its words as they are.
/// Nothing of it is edited.
class CollisionForm {
public:
    /// `selected` is how many nodes are selected, of which this is the last.
    void drawPolygon(const Scene& scene, const CollisionPolygonRef& where, usize selected) const;

    void drawWaterBox(const Scene& scene, const WaterBoxRef& where, usize selected) const;
};

} // namespace toki::scene
