#pragma once

#include "app/actor/domain/ActorEntry.h"

#include <span>
#include <string>
#include <vector>

namespace toki::actor {

/// Edits a variant's match by its entry's fields' own controls: a flag's
/// state, the values of an enum it may have, a list of numbers, and Link's age
/// and the scenes, which every entry may match on. A field that is not ticked
/// is not matched on. Returns whether anything changed.
[[nodiscard]] bool drawMatchEditor(std::vector<FieldMatch>& match,
                                   std::span<const ParamField> fields);

/// A match as one line: `type = small, 6  ·  age = child`, or `every placement`.
[[nodiscard]] std::string describeMatch(std::span<const FieldMatch> match);

} // namespace toki::actor
