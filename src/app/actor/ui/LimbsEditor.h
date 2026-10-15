#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// The colour limbs are tagged and badged in.
inline constexpr ImVec4 LimbColor{0.851F, 0.659F, 1.0F, 1.0F};

/// A skeleton part's limbs that draw other than their own display list, under
/// their own heading and outside any table of properties: one row each, its
/// number, name and what it draws instead, nothing or a display list of the
/// skeleton's object or another, opened to edit it. Limbs are added from a
/// list of the skeleton's, `object`'s `skeleton`. Returns whether anything
/// changed.
[[nodiscard]] bool editLimbs(std::vector<LimbOverride>& limbs, const std::string& skeleton,
                             const std::optional<std::string>& object, const EditContext& context);

/// The limb of `skeleton` in `object` a display list part is placed at, or
/// none, picked from the skeleton's limbs. Returns whether it changed.
[[nodiscard]] bool editLimbPlacement(std::optional<u8>& limb, const std::string& skeleton,
                                     const std::optional<std::string>& object,
                                     const EditContext& context);

} // namespace toki::actor
