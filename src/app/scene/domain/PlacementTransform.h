#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/viewport/domain/TransformSession.h"

#include <glm/ext/vector_float3.hpp>

namespace toki::scene {

/// `placement` moved to `point`, rounded to whole units, and, when `result`
/// turns, turned as it says: an actor or player entry about any axis, a
/// transition actor, which has Y alone, only about Y.
[[nodiscard]] Placement makeTransformedPlacement(const Placement& placement, const glm::vec3& point,
                                                 const viewport::TransformResult& result);

/// Puts `placement` where `where` names in `layer`, as a preview of an edit
/// shows it without making it; false when `layer` holds nothing there.
bool setLayerPlacement(SceneLayer& layer, const PlacementRef& where, const Placement& placement);

} // namespace toki::scene
