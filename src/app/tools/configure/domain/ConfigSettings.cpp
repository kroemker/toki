#include "ConfigSettings.h"

#include "app/actor/domain/ReadableName.h"
#include "app/tools/Types.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace toki::tools {

namespace {

struct ListedChoice {
    std::string_view prefix;
    std::span<const std::string_view> options;
};

constexpr std::array<std::string_view, 14> Buttons{
    "BTN_A",      "BTN_B", "BTN_Z", "BTN_START", "BTN_DUP",   "BTN_DDOWN", "BTN_DLEFT",
    "BTN_DRIGHT", "BTN_L", "BTN_R", "BTN_CUP",   "BTN_CDOWN", "BTN_CLEFT", "BTN_CRIGHT"};
constexpr std::array<std::string_view, 4> ControllerPorts{"CONTROLLER_PORT_1", "CONTROLLER_PORT_2",
                                                          "CONTROLLER_PORT_3", "CONTROLLER_PORT_4"};
constexpr std::array<std::string_view, 2> LinkAges{"LINK_AGE_ADULT", "LINK_AGE_CHILD"};
constexpr std::array<std::string_view, 5> BootTimes{"NEXT_TIME_NONE", "NEXT_TIME_DAY",
                                                    "NEXT_TIME_NIGHT", "NEXT_TIME_DAY_SET",
                                                    "NEXT_TIME_NIGHT_SET"};

constexpr std::array<ListedChoice, 4> ListedChoices{{
    {.prefix = "BTN_", .options = Buttons},
    {.prefix = "CONTROLLER_PORT_", .options = ControllerPorts},
    {.prefix = "LINK_AGE_", .options = LinkAges},
    {.prefix = "NEXT_TIME_", .options = BootTimes},
}};

struct CollectionChoice {
    std::string_view prefix;
    std::string_view collection;
};

constexpr std::array<CollectionChoice, 3> CollectionChoices{{
    {.prefix = "ENTR_", .collection = "EntranceIndex"},
    {.prefix = "SCENE_", .collection = "SceneID"},
    {.prefix = "NA_BGM_", .collection = "NA_BGM"},
}};

[[nodiscard]] std::string makeLower(const std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](const char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return lowered;
}

[[nodiscard]] std::string removeSpaces(const std::string_view text) {
    std::string kept;
    std::ranges::copy_if(text, std::back_inserter(kept), [](const char letter) {
        return std::isspace(static_cast<unsigned char>(letter)) == 0;
    });
    return kept;
}

[[nodiscard]] bool isInteger(std::string_view text) {
    if (text.starts_with('-')) {
        text.remove_prefix(1);
    }
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    while (!text.empty() &&
           (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
        text.remove_suffix(1);
    }
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return !text.empty() && error == std::errc() && end == text.data() + text.size();
}

[[nodiscard]] bool isDecimal(std::string_view text) {
    if (text.ends_with('f') || text.ends_with('F')) {
        text.remove_suffix(1);
    }
    if (!text.contains('.')) {
        return false;
    }
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

[[nodiscard]] ConfigSetting makeSetting(const hackeroot::ConfigDefine& define) {
    ConfigSetting setting{.define = define,
                          .label = actor::makeReadableName(makeLower(define.name))};
    const std::string_view written = define.written;
    if (written == "true" || written == "false") {
        setting.control = SettingControl::Toggle;
    } else if (isInteger(written)) {
        setting.control = SettingControl::Integer;
    } else if (isDecimal(written)) {
        setting.control = SettingControl::Decimal;
    }
    for (const ListedChoice& listed : ListedChoices) {
        if (written.starts_with(listed.prefix)) {
            setting.control = SettingControl::Choice;
            setting.options.assign(listed.options.begin(), listed.options.end());
        }
    }
    for (const CollectionChoice& choice : CollectionChoices) {
        if (written.starts_with(choice.prefix)) {
            setting.control = SettingControl::Choice;
            setting.collection = choice.collection;
        }
    }
    return setting;
}

[[nodiscard]] std::string makeGroupTitle(const std::filesystem::path& file) {
    std::string stem = file.stem().string();
    if (stem.starts_with("config_")) {
        stem.erase(0, std::string_view("config_").size());
    }
    return actor::makeReadableName(stem);
}

} // namespace

std::vector<ConfigGroup> makeConfigGroups(const std::span<const hackeroot::ConfigDefine> defines) {
    std::vector<ConfigGroup> groups;
    for (const hackeroot::ConfigDefine& define : defines) {
        if (groups.empty() || groups.back().file != define.file) {
            groups.push_back(
                ConfigGroup{.file = define.file, .title = makeGroupTitle(define.file)});
        }
        groups.back().settings.push_back(makeSetting(define));
    }
    return groups;
}

bool isOverridden(const hackeroot::ConfigDefine& define) {
    return !define.effective || removeSpaces(*define.effective) != removeSpaces(define.written);
}

bool isValidValue(const ConfigSetting& setting, const std::string_view value) {
    switch (setting.control) {
    case SettingControl::Toggle:
        return value == "true" || value == "false";
    case SettingControl::Integer:
        return isInteger(value);
    case SettingControl::Decimal:
        return isDecimal(value) || isInteger(value);
    case SettingControl::Choice:
        return !value.empty() &&
               (setting.options.empty() || std::ranges::contains(setting.options, value));
    case SettingControl::Text:
        return !value.empty();
    }
    return false;
}

std::expected<hackeroot::EditPlan, std::string>
makeConfigPlan(const std::filesystem::path& repository, const std::span<const ConfigGroup> groups,
               const std::map<std::string, std::string, std::less<>>& values) {
    hackeroot::EditPlan plan;
    for (const ConfigGroup& group : groups) {
        std::vector<const ConfigSetting*> changed;
        for (const ConfigSetting& setting : group.settings) {
            const auto value = values.find(setting.define.name);
            if (value != values.end() && value->second != setting.define.written) {
                changed.push_back(&setting);
            }
        }
        if (changed.empty()) {
            continue;
        }
        std::expected<std::string, std::string> text =
            hackeroot::readCheckoutText(repository / group.file);
        if (!text) {
            return std::unexpected(text.error());
        }
        hackeroot::FileEdit edit{.file = group.file, .before = *text, .after = *text};
        for (const ConfigSetting* setting : changed) {
            const std::string& value = values.find(setting->define.name)->second;
            if (!isValidValue(*setting, value)) {
                return std::unexpected(
                    std::format("'{}' is not a value {} can be", value, setting->define.name));
            }
            const hackeroot::EditStatus status =
                hackeroot::setDefine(edit.after, setting->define.line, setting->define.name, value);
            if (status == hackeroot::EditStatus::Add) {
                return std::unexpected(std::format(
                    "{}:{} no longer defines {}; read the configuration again",
                    group.file.generic_string(), setting->define.line, setting->define.name));
            }
            edit.items.push_back(
                hackeroot::EditItem{.status = status,
                                    .label = std::format("{}: {} → {}", setting->define.name,
                                                         setting->define.written, value)});
            if (isOverridden(setting->define)) {
                plan.warnings.push_back(std::format(
                    "{} is {} once config_safeguards.h has had its say, so the build may not "
                    "see {}",
                    setting->define.name, setting->define.effective.value_or("undefined"), value));
            }
        }
        plan.files.push_back(std::move(edit));
    }
    return plan;
}

} // namespace toki::tools
