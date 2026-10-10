#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"

#include <vector>

namespace toki::actor {

/// The params an overlay's main file reads with a literal
/// `PARAMS_GET_U(<actor>.params, shift, width)` or `PARAMS_GET_S`, in the
/// order first read. Each is named after what it is read into,
/// `this->type = ...` as "Type", or what it is passed to,
/// `Flags_GetSwitch(play, ...)` as "Switch Flag", and takes its values from a
/// collection when it is passed whole, or negated, to a function whose
/// argument has one: `Actor_OfferGetItem` a `GetItemID`, `Item_DropCollectible`
/// an `Item00Type`, `Actor_Spawn` an `ActorID`. One bit with no such name is a
/// flag. A read through a macro of the overlay's own, or of bits that overlap a
/// param found before, is left out.
[[nodiscard]] std::vector<ActorParam> findActorParams(const OverlayCode& code);

} // namespace toki::actor
