#include "ActorDatabase.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"

#include <algorithm>
#include <bit>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace toki::actor {

namespace {

[[nodiscard]] u16 readSource(const ParamSource source, const PlacementValues& placement) {
    switch (source) {
    case ParamSource::Params:
        return placement.params;
    case ParamSource::RotationX:
        return static_cast<u16>(placement.rotation[0]);
    case ParamSource::RotationY:
        return static_cast<u16>(placement.rotation[1]);
    case ParamSource::RotationZ:
        return static_cast<u16>(placement.rotation[2]);
    }
    return 0;
}

[[nodiscard]] bool isBuiltInMatching(const ParamMatch& match, const PlacementValues& placement) {
    std::optional<std::string> value;
    if (match.param == "age" && placement.age) {
        value = *placement.age == Age::Child ? "child" : "adult";
    } else if (match.param == "scene") {
        value = placement.scene;
    }
    return value && std::ranges::any_of(match.values, [&value](const MatchValue& candidate) {
               const std::string* name = std::get_if<std::string>(&candidate);
               return name != nullptr && *name == *value;
           });
}

[[nodiscard]] bool isValueMatching(const MatchValue& candidate, const s64 value) {
    if (const bool* flag = std::get_if<bool>(&candidate)) {
        return (value != 0) == *flag;
    }
    if (const s64* number = std::get_if<s64>(&candidate)) {
        return value == *number;
    }
    return false;
}

} // namespace

u8 findMaskShift(const u16 mask) {
    return mask == 0 ? 0 : static_cast<u8>(std::countr_zero(mask));
}

u8 findMaskWidth(const u16 mask) {
    return mask == 0 ? 0 : static_cast<u8>(16 - std::countl_zero(mask) - std::countr_zero(mask));
}

bool isMaskContiguous(const u16 mask) {
    return mask != 0 && std::popcount(mask) == findMaskWidth(mask);
}

std::optional<usize> findBitOwner(const std::span<const ActorParam> params,
                                  const ParamSource source, const u8 bit,
                                  const std::optional<usize> except) {
    for (usize i = 0; i < params.size(); ++i) {
        if (i != except && params[i].source == source && (params[i].mask & (1U << bit)) != 0) {
            return i;
        }
    }
    return std::nullopt;
}

void renameParam(ActorEntry& entry, const std::string& from, const std::string& to) {
    for (ActorParam& param : entry.params) {
        if (param.name == from) {
            param.name = to;
        }
    }
    for (ActorVariant& variant : entry.variants) {
        for (ParamMatch& match : variant.match) {
            if (match.param == from) {
                match.param = to;
            }
        }
    }
}

bool isMatchingScene(const ActorVariant& variant, const std::optional<std::string>& scene) {
    const auto match = std::ranges::find(variant.match, "scene", &ParamMatch::param);
    return match == variant.match.end() ||
           isBuiltInMatching(*match, PlacementValues{.scene = scene});
}

bool isBuiltInParam(const std::string_view name) {
    return name == "age" || name == "scene";
}

const ActorEntry* findEntry(const ActorDatabase& database, const std::string_view actor) {
    const auto found = database.entries.find(actor);
    return found == database.entries.end() ? nullptr : &found->second;
}

s64 readParamValue(const ActorParam& param, const PlacementValues& placement) {
    const u32 bits = readSource(param.source, placement);
    const u8 width = findMaskWidth(param.mask);
    const u32 raw = (bits & param.mask) >> findMaskShift(param.mask);
    if (param.isSigned && width > 0 && (raw & (1U << (width - 1U))) != 0) {
        return static_cast<s64>(raw) - (s64{1} << width);
    }
    return raw;
}

bool isMatching(const ActorEntry& entry, const ActorVariant& variant,
                const PlacementValues& placement) {
    return std::ranges::all_of(variant.match, [&](const ParamMatch& match) {
        if (isBuiltInParam(match.param)) {
            return isBuiltInMatching(match, placement);
        }
        const auto param = std::ranges::find(entry.params, match.param, &ActorParam::name);
        if (param == entry.params.end()) {
            return false;
        }
        const s64 value = readParamValue(*param, placement);
        return std::ranges::any_of(match.values, [value](const MatchValue& candidate) {
            return isValueMatching(candidate, value);
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
