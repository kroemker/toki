#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

class OverlayCode;

/// The values a choice param can take, read from the checkout rather than
/// written in the actor database, so they follow the checkout: an enumeration
/// of its headers, `GetItemID`, or one of its ordinal tables, `ActorID`. Each
/// value is labelled with its constant's name, `GI_HEART_PIECE`.
struct ValueCollection {
    std::string name;
    std::vector<ChoiceValue> values;
};

/// Where a collection every actor can use is read from: the enumeration of
/// that name in `header`, under `include/`, or, without a header, one of the
/// tables the workspace reads.
struct SharedCollection {
    std::string_view name;
    std::string_view header;
};

/// The collections every actor can use: get items, items and their models, the
/// collectibles `En_Item00` drops, sound effects, music, scene draw configs,
/// and the actor, object, scene and entrance tables.
[[nodiscard]] std::span<const SharedCollection> getSharedCollections();

/// Collections read, what went wrong reading them, and every file read, for
/// watching them for changes.
struct CollectionsRead {
    std::vector<ValueCollection> collections;
    std::vector<base::Diagnostic> diagnostics;
    std::vector<std::filesystem::path> files;
};

/// Reads every shared collection of `workspace`'s checkout, as the build
/// compiles the headers. A collection the checkout does not have is a
/// diagnostic naming it and where it was looked for.
[[nodiscard]] CollectionsRead readSharedCollections(const hackeroot::Workspace& workspace);

/// Reads the enumerations `actor`'s overlay headers define, each a collection
/// named by its tag or typedef, `EnBoxType`, and the tables its C defines
/// (`listTableCollections`). An actor without an overlay has none.
[[nodiscard]] CollectionsRead readActorCollections(const hackeroot::Workspace& workspace,
                                                   const hackeroot::ActorEntry& actor);

/// The tables `code` defines whose every element is a constant, each a
/// collection named as the array is, `sGetItemIds`, of its positions labelled
/// by their constants: `{ GI_RUPEE_GOLD, GI_WALLET_ADULT }` is 0, labelled
/// `GI_RUPEE_GOLD`, and 1. An element may be a number, labelled as written, or
/// a named constant with constants added, taken or masked off, labelled by the
/// first: `NA_SE_EV_WATER_WALL - SFX_FLAG`. A table with no named constant, of
/// symbols or of structs, is none.
[[nodiscard]] std::vector<ValueCollection> listTableCollections(const OverlayCode& code);

/// The collection of a name, as an actor sees it, or nothing when there is none.
using FindCollection = std::function<const ValueCollection*(std::string_view name)>;

/// What is wrong with how `entry`'s params use collections, that only the
/// checkout can tell: a collection it does not have, one whose values mostly do
/// not fit in a param's bits once its offset is taken off, which is the wrong
/// collection or the wrong bits rather than a sentinel such as `ITEM00_NONE`,
/// and matches on values that are not among them.
[[nodiscard]] std::vector<base::Diagnostic> checkCollections(const ActorEntry& entry,
                                                             const FindCollection& findCollection);

} // namespace toki::actor
