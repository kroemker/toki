#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// A variant's parts, each one row that reads as a sentence, "Display list
/// gPotDL from gameplay_dangeon_keep at 0, 52, 0": its tokens open pickers of
/// objects and symbols, and its "⋯" what is rarely set, its placement, setup
/// display list, pass, hilite, billboard, colours and segments. Rows move up
/// and down, are copied and removed, and display lists and skeletons are added.
/// Symbols are picked from `object` unless a part names its own. Returns
/// whether anything changed.
[[nodiscard]] bool drawPartsEditor(std::vector<ActorPart>& parts,
                                   const std::optional<std::string>& object,
                                   const EditContext& context);

} // namespace toki::actor
