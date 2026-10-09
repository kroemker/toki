#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// Edits a variant's steps in order: each one's own settings, moving it up or
/// down, removing it, and adding one of any kind; a skeleton's limbs each
/// with steps of their own. Symbols are picked from `object` unless a step
/// names its own. A limb's steps cannot draw a skeleton or a shadow. Returns
/// whether anything changed.
[[nodiscard]] bool drawStepsEditor(std::vector<DrawStep>& steps,
                                   const std::optional<std::string>& object,
                                   const EditContext& context, bool isInLimb);

} // namespace toki::actor
