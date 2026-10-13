#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::actor {

/// A part's segments, 8 to 12, as rows of its properties: each pointed at a
/// symbol of `object`, or built, as the game builds a list at run time, from
/// colours and tile sizes. A segment is pointed or left at nothing by buttons.
/// Returns whether anything changed.
[[nodiscard]] bool editSegments(std::vector<PartSegment>& segments,
                                const std::optional<std::string>& object,
                                const EditContext& context);

/// The badges a part's row shows for its segments: how many point at symbols,
/// and each built one, in the colour it sets.
[[nodiscard]] std::vector<RowBadge> makeSegmentBadges(std::span<const PartSegment> segments);

} // namespace toki::actor
