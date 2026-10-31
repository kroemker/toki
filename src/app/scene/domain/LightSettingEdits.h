#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"

#include <expected>

namespace toki::scene {

class SceneModel;

/// Sets the light setting at `index` of the scene's header for `layer`, the
/// game's scene layer, made its own for the layer first, as placements are.
[[nodiscard]] std::expected<void, base::Diagnostic>
setLightSetting(SceneModel& model, usize layer, usize index, const LightSetting& setting);

/// Adds `setting` to the end of the layer's light settings, and returns its
/// index. A header without a list is given one.
[[nodiscard]] std::expected<usize, base::Diagnostic> addLightSetting(SceneModel& model, usize layer,
                                                                     const LightSetting& setting);

/// Removes the light setting at `index`. The settings after it move down one,
/// so where the time of day picks the setting, each takes the time of the one
/// before it. The last one is kept: the game lights every scene by one.
[[nodiscard]] std::expected<void, base::Diagnostic> removeLightSetting(SceneModel& model,
                                                                       usize layer, usize index);

} // namespace toki::scene
