#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"

#include <string>
#include <vector>

namespace toki::actor {

/// One param of a placed actor, read by its entry: its value, and the value as
/// the panels show it, a flag as yes or no, a choice by its label, a number in
/// hexadecimal when the param says so.
struct ParamReading {
    std::string name;
    s64 value = 0;
    std::string text;
    std::string description;
};

/// `value` of `param` as the panels show it.
[[nodiscard]] std::string makeParamText(const ActorParam& param, s64 value);

/// Every param of `entry` read for `placement`, in the entry's order: by
/// source, then by their lowest bit.
[[nodiscard]] std::vector<ParamReading> readParams(const ActorEntry& entry,
                                                   const PlacementValues& placement);

/// The match that holds for exactly the values `placement` gives every param of
/// `entry`: numbers, and flags as true or false.
[[nodiscard]] std::vector<ParamMatch> makeParamsMatch(const ActorEntry& entry,
                                                      const PlacementValues& placement);

} // namespace toki::actor
