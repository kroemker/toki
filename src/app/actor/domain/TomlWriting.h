#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

/// What writing `actors.toml` builds its tables with, shared by the entry
/// and step writers. Internal to the actor database's writing.
[[nodiscard]] inline double makeNumber(const f32 value) {
    return std::stod(std::format("{}", value));
}

[[nodiscard]] inline toml::value<s64> makeHex(const s64 value) {
    toml::value<s64> hex(value);
    hex.flags(toml::value_flags::format_as_hexadecimal);
    return hex;
}

[[nodiscard]] inline toml::table makeInline() {
    toml::table table;
    table.is_inline(true);
    return table;
}

template <usize Count>
[[nodiscard]] toml::array makeNumbers(const std::array<f32, Count>& values) {
    toml::array array;
    for (const f32 value : values) {
        array.push_back(makeNumber(value));
    }
    return array;
}

template <usize Count>
[[nodiscard]] toml::array makeChannels(const std::array<u8, Count>& channels) {
    toml::array array;
    for (const u8 channel : channels) {
        array.push_back(s64{channel});
    }
    return array;
}

[[nodiscard]] inline bool isZero(const std::array<f32, 3>& values) {
    return std::ranges::all_of(values, [](const f32 value) { return value == 0.0F; });
}

inline void insertText(toml::table& table, const std::string_view key, const std::string& text) {
    if (!text.empty()) {
        table.insert(key, text);
    }
}

inline void insertOptional(toml::table& table, const std::string_view key,
                           const std::optional<std::string>& text) {
    if (text) {
        table.insert(key, *text);
    }
}

inline void insertOptional(toml::table& table, const std::string_view key,
                           const std::optional<f32>& value) {
    if (value) {
        table.insert(key, makeNumber(*value));
    }
}

[[nodiscard]] inline toml::table makeColors(const DrawColors& colors) {
    toml::table table = makeInline();
    if (colors.primitive) {
        table.insert("prim", makeChannels(*colors.primitive));
    }
    if (colors.environment) {
        table.insert("env", makeChannels(*colors.environment));
    }
    return table;
}

inline void insertPlacement(toml::table& table, const PartPlacement& placement) {
    if (!isZero(placement.translation)) {
        table.insert("translation", makeNumbers(placement.translation));
    }
    if (!isZero(placement.rotation)) {
        table.insert("rotation", makeNumbers(placement.rotation));
    }
    const std::array<f32, 3>& scale = placement.scale;
    if (scale[0] == scale[1] && scale[1] == scale[2]) {
        if (scale[0] != 1.0F) {
            table.insert("scale", makeNumber(scale[0]));
        }
    } else {
        table.insert("scale", makeNumbers(scale));
    }
}

[[nodiscard]] inline toml::table makeShadow(const Shadow& shadow) {
    toml::table table = makeInline();
    table.insert("kind", std::string(getEntryName(ShadowKindNames, shadow.kind)));
    table.insert("size", makeNumber(shadow.size));
    if (shadow.alpha != 0xFF) {
        table.insert("alpha", s64{shadow.alpha});
    }
    return table;
}

/// `steps` as `actors.toml` writes them.
[[nodiscard]] toml::array makeSteps(const std::vector<DrawStep>& steps);

} // namespace toki::actor
