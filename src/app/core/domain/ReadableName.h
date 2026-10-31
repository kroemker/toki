#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// An identifier of the C as words, for people: `RiverSoundType` is
/// "River Sound Type", `ActorID` "Actor ID", `NA_BGM` "NA BGM",
/// `soundEffects` "Sound Effects". A static's `s` prefix is dropped, so
/// `sGetItemIds` is "Get Item Ids".
[[nodiscard]] std::string makeReadableName(std::string_view identifier);

/// The constants of one enumeration, or of one family of macros, as words for
/// people, in the order given: the longest prefix of whole words every
/// constant shares dropped, as long as each keeps a word, and then each word
/// with only its first letter in capitals. `GI_HEART_PIECE` among the get
/// items is "Heart Piece". Abbreviations written in capitals (`ID`, `BGM`,
/// `SFX`, ...) and words starting with a digit are kept as written. A name
/// that is no constant in capitals, a number or `sTable`, is kept as written
/// and shares no prefix; nothing is dropped from a single constant.
[[nodiscard]] std::vector<std::string> makeConstantLabels(std::span<const std::string_view> names);
[[nodiscard]] std::vector<std::string> makeConstantLabels(std::span<const std::string> names);

/// One constant as words, as `makeConstantLabels` makes them, with `prefix`
/// dropped when it starts with it: `SCENE_HYRULE_FIELD` with `SCENE_` is
/// "Hyrule Field".
[[nodiscard]] std::string makeConstantLabel(std::string_view name, std::string_view prefix);

} // namespace toki::core
