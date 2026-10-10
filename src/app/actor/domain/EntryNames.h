#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace toki::actor {

/// An enumerator, how `actors.toml` spells it, and how the panels show it.
template <typename Enum>
struct EntryName {
    Enum value;
    std::string_view name;
    std::string_view label;
};

inline constexpr std::array ParamKindNames = {
    EntryName<ParamKind>{ParamKind::Flag, "flag", "Flag"},
    EntryName<ParamKind>{ParamKind::Number, "number", "Number"},
    EntryName<ParamKind>{ParamKind::Choice, "choice", "Choice"},
};

inline constexpr std::array ParamSourceNames = {
    EntryName<ParamSource>{ParamSource::Params, "params", "Params"},
    EntryName<ParamSource>{ParamSource::RotationX, "rotation_x", "Rotation X"},
    EntryName<ParamSource>{ParamSource::RotationY, "rotation_y", "Rotation Y"},
    EntryName<ParamSource>{ParamSource::RotationZ, "rotation_z", "Rotation Z"},
};

inline constexpr std::array PassNames = {
    EntryName<Pass>{Pass::Opaque, "opaque", "Opaque"},
    EntryName<Pass>{Pass::Translucent, "translucent", "Translucent"},
};

inline constexpr std::array DrawKindNames = {
    EntryName<DrawKind>{DrawKind::Model, "model", "Model"},
    EntryName<DrawKind>{DrawKind::Marker, "marker", "Marker"},
    EntryName<DrawKind>{DrawKind::Children, "children", "Children only"},
};

inline constexpr std::array AgeNames = {
    EntryName<Age>{Age::Child, "child", "Child"},
    EntryName<Age>{Age::Adult, "adult", "Adult"},
};

template <typename Enum, usize Count>
[[nodiscard]] constexpr std::optional<Enum>
findEntryValue(const std::array<EntryName<Enum>, Count>& names, const std::string_view name) {
    const auto found = std::ranges::find(names, name, &EntryName<Enum>::name);
    return found == names.end() ? std::nullopt : std::optional(found->value);
}

template <typename Enum, usize Count>
[[nodiscard]] constexpr std::string_view
getEntryName(const std::array<EntryName<Enum>, Count>& names, const Enum value) {
    const auto found = std::ranges::find(names, value, &EntryName<Enum>::value);
    return found == names.end() ? std::string_view() : found->name;
}

template <typename Enum, usize Count>
[[nodiscard]] constexpr std::string_view
getEntryLabel(const std::array<EntryName<Enum>, Count>& names, const Enum value) {
    const auto found = std::ranges::find(names, value, &EntryName<Enum>::value);
    return found == names.end() ? std::string_view() : found->label;
}

} // namespace toki::actor
