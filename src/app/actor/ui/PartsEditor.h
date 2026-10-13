#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::actor {

/// A variant's parts, one row each: a tag for its kind, its symbol, its own
/// object when it names one, and badges for what is set, "faces cam", "xlu",
/// "shiny". Clicking a row opens its properties below it: symbol, object,
/// pose, the limbs a skeleton draws otherwise, the limb a display list is
/// placed at, placement, billboard, colours, pass, setup display list, hilite
/// and segments. A display list placed at a limb is indented. An item is a
/// get-item draw ID, or the param that picks one and the table it indexes. Rows move up, are copied
/// and removed, and display lists and skeletons are added. Symbols are picked from `object` unless
/// a part names its own. The buttons that add stay on the last line, for what is added beside them.
/// Returns whether anything changed.
[[nodiscard]] bool drawPartsEditor(std::vector<ActorPart>& parts,
                                   const std::optional<std::string>& object,
                                   std::span<const ActorParam> params, const EditContext& context);

} // namespace toki::actor
