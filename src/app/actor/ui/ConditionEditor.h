#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ValueCollection.h"

#include <span>
#include <string>
#include <vector>

namespace toki::actor {

/// A variant's match as chips that wrap where the panel ends, "Type Small or
/// Lens Small", values cut to fit, each opening its param's own control, a
/// choice's values, a flag's state, a list of numbers, Link's age or the
/// scenes, with one to remove it and one to add another. Returns whether
/// anything changed.
[[nodiscard]] bool drawConditionEditor(std::vector<ParamMatch>& match,
                                       std::span<const ActorParam> params,
                                       const FindCollection& findCollection);

/// A match as one line: `Type is Small or Lens Small, and Age is Child`, or
/// `Every placement`.
[[nodiscard]] std::string describeCondition(std::span<const ParamMatch> match,
                                            std::span<const ActorParam> params,
                                            const FindCollection& findCollection);

} // namespace toki::actor
