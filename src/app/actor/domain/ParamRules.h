#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
#include "cparser/Token.h"

#include <optional>
#include <span>
#include <vector>

namespace toki::actor {

/// The params an overlay's main file reads with a literal
/// `PARAMS_GET_U(<actor>.params, shift, width)` or `PARAMS_GET_S`, in the
/// order first read. Each is named after what it is read into,
/// `this->type = ...` as "Type", or what it is passed to,
/// `Flags_GetSwitch(play, ...)` as "Switch Flag", and takes its values from a
/// collection when it is passed whole, or negated, to a function whose
/// argument has one: `Actor_OfferGetItem` a `GetItemID`, `Item_DropCollectible`
/// an `Item00Type`, `Actor_Spawn` an `ActorID`, `Actor_PlaySfx` a `SfxId`,
/// `Audio_PlayFanfare` an `NA_BGM`. One bit with no such name is a
/// flag. A read that picks an entry of one of the overlay's tables of
/// constants (`listTableCollections`) takes its values from that table, as
/// does one of the whole params, or of the bits `Init` narrowed them to. A
/// read through a macro of the overlay's own, or of bits that overlap a
/// param found before, is left out.
[[nodiscard]] std::vector<ActorParam> findActorParams(const OverlayCode& code);

/// The bits of the params an index picks by, and what is added to them before
/// they pick.
struct IndexRead {
    u16 mask = 0;
    bool isSigned = false;
    s64 offset = 0;
};

/// Which params an index in `body` picks by, as `sDLists[...]` is written: a
/// literal `PARAMS_GET_U` of them, the whole params, or the bits `Init` narrowed
/// them to, or a local assigned one read once, each with a literal added or
/// taken, `PARAMS_GET_U(...) - 1`. Anything else is not followed.
[[nodiscard]] std::optional<IndexRead> findIndexRead(const OverlayCode& code,
                                                     std::span<const cparser::Token> body,
                                                     std::span<const cparser::Token> index);

} // namespace toki::actor
