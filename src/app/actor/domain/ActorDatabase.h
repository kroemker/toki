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

/// The actors one file says how to show, by enum name, and the HackerOoT
/// commit the file was checked against, for Toki's own. `diagnostics` is what
/// reading the file left out; `problems` is what `checkActorEntry` finds wrong
/// with the entries read, which are kept as they are written.
struct ActorDatabase {
    std::optional<std::string> checkedAgainst;
    std::map<std::string, ActorEntry, std::less<>> entries;
    std::vector<base::Diagnostic> diagnostics;
    std::vector<base::Diagnostic> problems;
};

/// The entry of `database` for `actor`, if it has one.
[[nodiscard]] const ActorEntry* findEntry(const ActorDatabase& database, std::string_view actor);

/// What fields are read from for one placed actor: its params and rotation,
/// Link's age in the scene layer it is placed in, when the layer has one, and
/// the scene's enum name, `SCENE_DEKU_TREE`.
struct PlacementValues {
    u16 params = 0;
    std::array<s16, 3> rotation{};
    std::optional<Age> age;
    std::optional<std::string> scene;
};

/// Whether `variant` can match a placement in the scene of enum name `scene`:
/// whether it has no match on the scene, or one that `scene` is among.
[[nodiscard]] bool isMatchingScene(const ActorVariant& variant,
                                   const std::optional<std::string>& scene);

/// Whether `name` is a param every entry has without declaring it: `age` and
/// `scene`.
[[nodiscard]] bool isBuiltInParam(std::string_view name);

/// A param's value for a placement: its bits shifted down, sign-extended when
/// it is signed.
[[nodiscard]] s64 readParamValue(const ActorParam& param, const PlacementValues& placement);

/// Whether every param of `variant`'s match is one of its values for
/// `placement`. A match on `age` or `scene` fails where the placement has none.
[[nodiscard]] bool isMatching(const ActorEntry& entry, const ActorVariant& variant,
                              const PlacementValues& placement);

/// What is wrong with `entry` that reading it cannot tell: params without bits,
/// masks whose bits are not adjacent, params of one source that share a bit, choices whose values
/// do not fit, params named as the built-in ones, matches on params the entry does not have,
/// without values or with values of the wrong kind, parts without a symbol or with a pose that is
/// not a skeleton's, and variants after one that matches everything, which are never drawn.
[[nodiscard]] std::vector<base::Diagnostic> checkActorEntry(const ActorEntry& entry);

/// The first variant of `entry` whose match holds for `placement`.
[[nodiscard]] const ActorVariant* findVariant(const ActorEntry& entry,
                                              const PlacementValues& placement);

} // namespace toki::actor
