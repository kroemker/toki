#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

/// The actors one file says how to draw, by enum name, and the HackerOoT
/// commit the file was checked against, for the shipped one.
struct ActorDatabase {
    std::optional<std::string> checkedAgainst;
    std::map<std::string, ActorEntry, std::less<>> entries;
    std::vector<base::Diagnostic> diagnostics;
};

/// Which file an entry was found in.
enum class EntryOrigin : u8 {
    Shipped,
    Checkout,
};

struct FoundEntry {
    const ActorEntry* entry = nullptr;
    EntryOrigin origin = EntryOrigin::Shipped;
};

/// The entry for `actor`: the checkout's when it has one, which replaces the
/// shipped one whole, or else the shipped one.
[[nodiscard]] std::optional<FoundEntry>
findActorEntry(const ActorDatabase& shipped, const ActorDatabase& checkout, std::string_view actor);

/// What fields are read from for one placed actor: its params and rotation,
/// and Link's age in the scene layer it is placed in, when the layer has one.
struct PlacementValues {
    u16 params = 0;
    std::array<s16, 3> rotation{};
    std::optional<Age> age;
};

/// A field's value for a placement: its bits, sign-extended when it is signed.
[[nodiscard]] s64 readFieldValue(const ParamField& field, const PlacementValues& placement);

/// Whether every field of `variant`'s match holds for `placement`. `age` is a
/// field every entry has; a match on it fails where the layer has no age.
[[nodiscard]] bool isMatching(const ActorEntry& entry, const ActorVariant& variant,
                              const PlacementValues& placement);

/// What is wrong with `entry` that reading it cannot tell: fields whose bits
/// do not fit or overlap, enum values that do not fit their field, matches on
/// fields the entry does not have or with values of the wrong kind, a variant
/// that draws both a skeleton and display lists, and variants after one that
/// matches everything, which are never drawn.
[[nodiscard]] std::vector<base::Diagnostic> checkActorEntry(const ActorEntry& entry);

/// The first variant of `entry` whose match holds for `placement`.
[[nodiscard]] const ActorVariant* findVariant(const ActorEntry& entry,
                                              const PlacementValues& placement);

} // namespace toki::actor
