#include "ActorDatabase.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace toki::actor {

namespace {

[[nodiscard]] u16 readSource(const FieldSource source, const PlacementValues& placement) {
    switch (source) {
    case FieldSource::Params:
        return placement.params;
    case FieldSource::RotationX:
        return static_cast<u16>(placement.rotation[0]);
    case FieldSource::RotationY:
        return static_cast<u16>(placement.rotation[1]);
    case FieldSource::RotationZ:
        return static_cast<u16>(placement.rotation[2]);
    }
    return 0;
}

[[nodiscard]] std::optional<s64> findEnumValue(const ParamField& field,
                                               const std::string_view name) {
    const auto found = std::ranges::find(field.values, name, &EnumValue::name);
    return found == field.values.end() ? std::nullopt : std::optional(found->value);
}

[[nodiscard]] bool isBuiltInMatching(const FieldMatch& match, const PlacementValues& placement) {
    std::optional<std::string> value;
    if (match.field == "age" && placement.age) {
        value = *placement.age == Age::Child ? "child" : "adult";
    } else if (match.field == "scene") {
        value = placement.scene;
    }
    return value && std::ranges::any_of(match.values, [&value](const MatchValue& candidate) {
               const std::string* name = std::get_if<std::string>(&candidate);
               return name != nullptr && *name == *value;
           });
}

[[nodiscard]] bool isValueMatching(const ParamField& field, const MatchValue& candidate,
                                   const s64 value) {
    if (const bool* flag = std::get_if<bool>(&candidate)) {
        return (value != 0) == *flag;
    }
    if (const s64* number = std::get_if<s64>(&candidate)) {
        return value == *number;
    }
    return findEnumValue(field, std::get<std::string>(candidate)) == value;
}

} // namespace

bool isBuiltInField(const std::string_view name) {
    return name == "age" || name == "scene";
}

std::optional<FoundEntry> findActorEntry(const ActorDatabase& shipped,
                                         const ActorDatabase& checkout,
                                         const std::string_view actor) {
    if (const auto found = checkout.entries.find(actor); found != checkout.entries.end()) {
        return FoundEntry{.entry = &found->second, .origin = EntryOrigin::Checkout};
    }
    if (const auto found = shipped.entries.find(actor); found != shipped.entries.end()) {
        return FoundEntry{.entry = &found->second, .origin = EntryOrigin::Shipped};
    }
    return std::nullopt;
}

s64 readFieldValue(const ParamField& field, const PlacementValues& placement) {
    const u32 bits = readSource(field.source, placement);
    const u32 mask = field.width >= 32 ? 0xFFFF'FFFFU : (1U << field.width) - 1U;
    const u32 raw = (bits >> field.shift) & mask;
    if (field.isSigned && field.width > 0 && (raw & (1U << (field.width - 1U))) != 0) {
        return static_cast<s64>(raw) - (s64{1} << field.width);
    }
    return raw;
}

bool isMatching(const ActorEntry& entry, const ActorVariant& variant,
                const PlacementValues& placement) {
    return std::ranges::all_of(variant.match, [&](const FieldMatch& match) {
        if (isBuiltInField(match.field)) {
            return isBuiltInMatching(match, placement);
        }
        const auto field = std::ranges::find(entry.fields, match.field, &ParamField::name);
        if (field == entry.fields.end()) {
            return false;
        }
        const s64 value = readFieldValue(*field, placement);
        return std::ranges::any_of(match.values, [&](const MatchValue& candidate) {
            return isValueMatching(*field, candidate, value);
        });
    });
}

const ActorVariant* findVariant(const ActorEntry& entry, const PlacementValues& placement) {
    const auto found = std::ranges::find_if(entry.variants, [&](const ActorVariant& variant) {
        return isMatching(entry, variant, placement);
    });
    return found == entry.variants.end() ? nullptr : &*found;
}

} // namespace toki::actor
