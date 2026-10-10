#include "ParamReading.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] std::string formatNumber(const ActorParam& param, const s64 value) {
    if (param.isHex) {
        return value < 0 ? std::format("-0x{:X}", -value) : std::format("0x{:X}", value);
    }
    return std::format("{}", value);
}

} // namespace

std::string makeParamText(const ActorParam& param, const s64 value,
                          const ValueCollection* collection) {
    if (param.kind == ParamKind::Flag) {
        return value != 0 ? "yes" : "no";
    }
    if (param.kind != ParamKind::Choice) {
        return formatNumber(param, value);
    }
    const s64 looked = value + param.offset;
    if (param.collection && collection == nullptr) {
        return formatNumber(param, looked);
    }
    const std::vector<ChoiceValue>& values =
        collection != nullptr ? collection->values : param.values;
    const auto found = std::ranges::find(values, looked, &ChoiceValue::value);
    if (found != values.end()) {
        return found->label;
    }
    return collection != nullptr
               ? std::format("{} (not in {})", formatNumber(param, looked), collection->name)
               : std::format("{} (no label)", formatNumber(param, looked));
}

const ValueCollection* findParamCollection(const ActorParam& param,
                                           const FindCollection& findCollection) {
    return param.collection && findCollection ? findCollection(*param.collection) : nullptr;
}

std::vector<ParamReading> readParams(const ActorEntry& entry, const PlacementValues& placement,
                                     const FindCollection& findCollection) {
    std::vector<ParamReading> readings;
    readings.reserve(entry.params.size());
    for (const ActorParam& param : entry.params) {
        const s64 value = readParamValue(param, placement);
        readings.push_back(ParamReading{
            .name = param.name,
            .value = value,
            .text = makeParamText(param, value, findParamCollection(param, findCollection)),
            .description = param.description});
    }
    return readings;
}

std::vector<ParamMatch> makeParamsMatch(const ActorEntry& entry, const PlacementValues& placement) {
    std::vector<ParamMatch> match;
    match.reserve(entry.params.size());
    for (const ActorParam& param : entry.params) {
        const s64 value = readParamValue(param, placement);
        match.push_back(ParamMatch{.param = param.name,
                                   .values = {param.kind == ParamKind::Flag ? MatchValue(value != 0)
                                                                            : MatchValue(value)}});
    }
    return match;
}

} // namespace toki::actor
