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

std::string makeParamText(const ActorParam& param, const s64 value) {
    if (param.kind == ParamKind::Flag) {
        return value != 0 ? "yes" : "no";
    }
    if (param.kind == ParamKind::Choice) {
        const auto found = std::ranges::find(param.values, value, &ChoiceValue::value);
        return found != param.values.end() ? found->label : std::format("{} (no label)", value);
    }
    return param.isHex ? std::format("0x{:X}", value) : std::format("{}", value);
}

std::vector<ParamReading> readParams(const ActorEntry& entry, const PlacementValues& placement) {
    std::vector<ParamReading> readings;
    readings.reserve(entry.params.size());
    for (const ActorParam& param : entry.params) {
        const s64 value = readParamValue(param, placement);
        readings.push_back(ParamReading{.name = param.name,
                                        .value = value,
                                        .text = makeParamText(param, value),
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
