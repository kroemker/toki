#pragma once

#include <string>
#include <string_view>

namespace toki::actor {

/// An identifier of the C as words, for people: `RiverSoundType` is
/// "River Sound Type", `ActorID` "Actor ID", `NA_BGM` "NA BGM",
/// `soundEffects` "Sound Effects". A static's `s` prefix is dropped, so
/// `sGetItemIds` is "Get Item Ids".
[[nodiscard]] std::string makeReadableName(std::string_view identifier);

} // namespace toki::actor
