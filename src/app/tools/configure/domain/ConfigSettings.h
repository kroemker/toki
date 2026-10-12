#pragma once

#include "app/tools/Types.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"

#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::tools {

/// How a setting is edited.
enum class SettingControl : u8 {
    Toggle,
    Integer,
    Decimal,
    Choice,
    Text,
};

/// One setting of the configuration and how it is edited, by what it is
/// written as: `true` and `false` toggled, a number typed, a name picked from
/// the names it may be, listed here for the game's buttons, controller ports,
/// ages and times of day, or a collection of the checkout for entrances,
/// scenes and music. Anything else is text.
struct ConfigSetting {
    hackeroot::ConfigDefine define;
    std::string label;
    SettingControl control = SettingControl::Text;
    std::vector<std::string> options;
    std::string collection;
};

/// The settings of one configuration header, under a title made of its name:
/// `config_debug_controls.h` is "Debug Controls".
struct ConfigGroup {
    std::filesystem::path file;
    std::string title;
    std::vector<ConfigSetting> settings;
};

/// The names a setting written as one starting with `prefix` may be, as
/// `LINK_AGE_` is `LINK_AGE_ADULT` or `LINK_AGE_CHILD`, or none.
[[nodiscard]] std::span<const std::string_view> findListedOptions(std::string_view prefix);

/// `defines`, grouped by their header in order.
[[nodiscard]] std::vector<ConfigGroup>
makeConfigGroups(std::span<const hackeroot::ConfigDefine> defines);

/// Whether the build ends with another value than `define` is written as, as
/// when `config_safeguards.h` overrides it.
[[nodiscard]] bool isOverridden(const hackeroot::ConfigDefine& define);

/// Whether `value` may be written for `setting`: a number for a number, one of
/// its names for a listed choice, and nothing empty.
[[nodiscard]] bool isValidValue(const ConfigSetting& setting, std::string_view value);

/// What writing `values`, by setting name, over the settings of `groups` does to
/// the configuration headers of the checkout at `repository`. A setting the
/// safeguards override is a warning, since the build will not see the value.
[[nodiscard]] std::expected<hackeroot::EditPlan, std::string>
makeConfigPlan(const std::filesystem::path& repository, std::span<const ConfigGroup> groups,
               const std::map<std::string, std::string, std::less<>>& values);

} // namespace toki::tools
