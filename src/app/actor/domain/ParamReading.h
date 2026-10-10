#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ValueCollection.h"

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

/// `value` of `param` as the panels show it. A choice of a collection is shown
/// by its constant's name from `collection`, the param's collection once read;
/// before, or for a value it lacks, by its number.
[[nodiscard]] std::string makeParamText(const ActorParam& param, s64 value,
                                        const ValueCollection* collection);

/// The collection `param` takes its values from, found by `findCollection`,
/// or nothing when it lists its own, or the collection is not read yet.
[[nodiscard]] const ValueCollection* findParamCollection(const ActorParam& param,
                                                         const FindCollection& findCollection);

/// Every param of `entry` read for `placement`, in the entry's order: by
/// source, then by their lowest bit, choices of collections found by
/// `findCollection`.
[[nodiscard]] std::vector<ParamReading> readParams(const ActorEntry& entry,
                                                   const PlacementValues& placement,
                                                   const FindCollection& findCollection);

/// The match that holds for exactly the values `placement` gives every param of
/// `entry`: numbers, and flags as true or false.
[[nodiscard]] std::vector<ParamMatch> makeParamsMatch(const ActorEntry& entry,
                                                      const PlacementValues& placement);

} // namespace toki::actor
