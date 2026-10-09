#include "FieldReading.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] const EnumValue* findValueName(const ParamField& field, const s64 value) {
    const auto found = std::ranges::find(field.values, value, &EnumValue::value);
    return found != field.values.end() ? &*found : nullptr;
}

} // namespace

std::string makeFieldText(const ParamField& field, const s64 value) {
    if (field.kind == FieldKind::Flag) {
        return value != 0 ? "yes" : "no";
    }
    if (field.kind == FieldKind::Enum) {
        const EnumValue* named = findValueName(field, value);
        return named != nullptr ? named->name : std::format("{} (unnamed)", value);
    }
    return field.isHex ? std::format("0x{:X}", value) : std::format("{}", value);
}

std::vector<FieldReading> readFields(const ActorEntry& entry, const PlacementValues& placement) {
    std::vector<FieldReading> readings;
    readings.reserve(entry.fields.size());
    for (const ParamField& field : entry.fields) {
        const s64 value = readFieldValue(field, placement);
        readings.push_back(FieldReading{.name = field.name,
                                        .source = field.source,
                                        .shift = field.shift,
                                        .value = value,
                                        .text = makeFieldText(field, value),
                                        .role = field.role,
                                        .meaning = field.meaning});
    }
    std::ranges::stable_sort(readings, {}, [](const FieldReading& reading) {
        return std::pair(reading.source, reading.shift);
    });
    return readings;
}

std::vector<FieldMatch> makeFieldsMatch(const ActorEntry& entry, const PlacementValues& placement) {
    std::vector<FieldMatch> match;
    match.reserve(entry.fields.size());
    for (const ParamField& field : entry.fields) {
        const s64 value = readFieldValue(field, placement);
        MatchValue matched = value;
        if (field.kind == FieldKind::Flag) {
            matched = value != 0;
        } else if (const EnumValue* named = findValueName(field, value)) {
            matched = named->name;
        }
        match.push_back(FieldMatch{.field = field.name, .values = {std::move(matched)}});
    }
    return match;
}

} // namespace toki::actor
