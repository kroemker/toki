#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"

#include <expected>
#include <string>

namespace toki::actor {

/// "Add a variant for these params": a variant matching exactly the values
/// `placement` gives every field, drawn as the variant it matches now and kept
/// to that variant's age and scene, ahead of
/// the others in the checkout's entry for the actor. An actor without one there
/// gets `current`, its entry from wherever it was found, copied in first, as a
/// checkout's entry replaces the shipped one whole.
[[nodiscard]] std::expected<void, std::string>
addVariantForParams(ActorDatabase& checkout, const ActorEntry& current,
                    const PlacementValues& placement);

/// The checkout's entry for `entry.actor` becomes `entry`, whole.
[[nodiscard]] std::expected<void, std::string> setCheckoutEntry(ActorDatabase& checkout,
                                                                ActorEntry entry);

/// The checkout's entry for `actor` is removed, so the shipped one, or a guess,
/// draws it again.
[[nodiscard]] std::expected<void, std::string> removeCheckoutEntry(ActorDatabase& checkout,
                                                                   const std::string& actor);

} // namespace toki::actor
