#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// Edits an entry's variants in the order they are tried: moving, copying and
/// removing each, its match, its own object, scale, height offset and draw
/// kind, what it draws, by keys or by steps, the actors it spawns, its spawn
/// condition and default params. Returns whether anything changed.
[[nodiscard]] bool drawVariantsEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
