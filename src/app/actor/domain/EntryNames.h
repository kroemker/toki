#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace toki::actor {

/// An enumerator and how `actors.toml` spells it.
template <typename Enum>
struct EntryName {
    Enum value;
    std::string_view name;
};

inline constexpr std::array FieldKindNames = {
    EntryName<FieldKind>{FieldKind::Flag, "flag"},
    EntryName<FieldKind>{FieldKind::Enum, "enum"},
    EntryName<FieldKind>{FieldKind::Integer, "integer"},
};

inline constexpr std::array FieldSourceNames = {
    EntryName<FieldSource>{FieldSource::Params, "params"},
    EntryName<FieldSource>{FieldSource::RotationX, "rotation_x"},
    EntryName<FieldSource>{FieldSource::RotationY, "rotation_y"},
    EntryName<FieldSource>{FieldSource::RotationZ, "rotation_z"},
};

inline constexpr std::array FieldRoleNames = {
    EntryName<FieldRole>{FieldRole::None, "none"},
    EntryName<FieldRole>{FieldRole::SwitchFlag, "switch_flag"},
    EntryName<FieldRole>{FieldRole::ChestFlag, "chest_flag"},
    EntryName<FieldRole>{FieldRole::CollectibleFlag, "collectible_flag"},
    EntryName<FieldRole>{FieldRole::ClearFlag, "clear_flag"},
    EntryName<FieldRole>{FieldRole::Path, "path"},
    EntryName<FieldRole>{FieldRole::TextId, "text_id"},
    EntryName<FieldRole>{FieldRole::Item, "item"},
    EntryName<FieldRole>{FieldRole::Actor, "actor"},
};

inline constexpr std::array AgeNames = {
    EntryName<Age>{Age::Child, "child"},
    EntryName<Age>{Age::Adult, "adult"},
};

inline constexpr std::array TimeNames = {
    EntryName<TimeOfDay>{TimeOfDay::Day, "day"},
    EntryName<TimeOfDay>{TimeOfDay::Night, "night"},
};

inline constexpr std::array PassNames = {
    EntryName<Pass>{Pass::Opaque, "opaque"},
    EntryName<Pass>{Pass::Translucent, "translucent"},
};

inline constexpr std::array DrawKindNames = {
    EntryName<DrawKind>{DrawKind::Model, "model"},
    EntryName<DrawKind>{DrawKind::Marker, "marker"},
    EntryName<DrawKind>{DrawKind::Children, "children"},
};

inline constexpr std::array ShadowKindNames = {
    EntryName<ShadowKind>{ShadowKind::None, "none"},
    EntryName<ShadowKind>{ShadowKind::Circle, "circle"},
    EntryName<ShadowKind>{ShadowKind::WhiteCircle, "white_circle"},
    EntryName<ShadowKind>{ShadowKind::Horse, "horse"},
    EntryName<ShadowKind>{ShadowKind::Feet, "feet"},
};

inline constexpr std::array StatusNames = {
    EntryName<EntryStatus>{EntryStatus::Guessed, "guessed"},
    EntryName<EntryStatus>{EntryStatus::Checked, "checked"},
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

} // namespace toki::actor
