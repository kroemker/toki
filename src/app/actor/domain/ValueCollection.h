#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

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
/// collectibles `En_Item00` drops, and the actor, object, scene and entrance
/// tables.
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
/// named by its tag or typedef, `EnBoxType`. An actor without an overlay has
/// none.
[[nodiscard]] CollectionsRead readActorCollections(const hackeroot::Workspace& workspace,
                                                   const hackeroot::ActorEntry& actor);

} // namespace toki::actor
