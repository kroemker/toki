#pragma once

#include "app/actor/domain/ActorEntry.h"

#include <vector>

namespace toki::actor {

/// Edits an entry's fields: each one's name, kind, source, bits, sign,
/// whether it is shown in hexadecimal, role, meaning, and an enum's values.
/// Returns whether anything changed.
[[nodiscard]] bool drawFieldsEditor(std::vector<ParamField>& fields);

} // namespace toki::actor
