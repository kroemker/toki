#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// One field of a placed actor's params, read by its entry: its value, and the
/// value as the panels show it, a flag as yes or no, an enum value by its name,
/// a number in hexadecimal when the field says so.
struct FieldReading {
    std::string name;
    FieldSource source = FieldSource::Params;
    u8 shift = 0;
    s64 value = 0;
    std::string text;
    FieldRole role = FieldRole::None;
    std::string meaning;
};

/// `value` of `field` as the panels show it.
[[nodiscard]] std::string makeFieldText(const ParamField& field, s64 value);

/// Every field of `entry` read for `placement`, in the order of their bits:
/// the params' first, then each rotation axis's.
[[nodiscard]] std::vector<FieldReading> readFields(const ActorEntry& entry,
                                                   const PlacementValues& placement);

/// The match that holds for exactly the values `placement` gives every field of
/// `entry`: enum values by name where they have one, flags as true or false.
[[nodiscard]] std::vector<FieldMatch> makeFieldsMatch(const ActorEntry& entry,
                                                      const PlacementValues& placement);

} // namespace toki::actor
