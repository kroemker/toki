#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"

#include <string_view>
#include <vector>

namespace toki::actor {

/// What a segment step points a segment at to point it at nothing, as `Draw`
/// functions point one at `gEmptyDL` of the game code.
inline constexpr std::string_view EmptySegment = "gEmptyDL";

/// The steps `variant` is drawn by: its own, or else those its keys say. A
/// skeleton is drawn in the variant's colours and segments; each display list
/// from the actor's matrix, by its own placement, colours and segments over
/// the variant's, with the segments only the lists before it set pointed at
/// nothing again, and facing the camera when it or the variant is a billboard.
[[nodiscard]] std::vector<DrawStep> makeDrawSteps(const ActorVariant& variant);

} // namespace toki::actor
