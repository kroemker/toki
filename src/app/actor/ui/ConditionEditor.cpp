#include "ConditionEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ParamReading.h"
#include "app/actor/ui/Sentence.h"
#include "app/core/ui/Icons.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] const ActorParam* findParam(const std::span<const ActorParam> params,
                                          const std::string_view name) {
    const auto found = std::ranges::find(params, name, &ActorParam::name);
    return found == params.end() ? nullptr : &*found;
}

[[nodiscard]] std::string describeValue(const MatchValue& value, const ActorParam* param) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        return *flag ? "set" : "not set";
    }
    if (const s64* number = std::get_if<s64>(&value)) {
        return param != nullptr ? makeParamText(*param, *number) : std::format("{}", *number);
    }
    const std::string& name = std::get<std::string>(value);
    if (name == "child" || name == "adult") {
        return name == "child" ? "Child" : "Adult";
    }
    return name;
}

[[nodiscard]] std::string describeMatch(const ParamMatch& match, const ActorParam* param) {
    const std::string name = match.param == "age"     ? "Age"
                             : match.param == "scene" ? "Scene"
                                                      : match.param;
    std::string values;
    for (usize i = 0; i < match.values.size(); ++i) {
        values += (i == 0 ? "" : " or ") + describeValue(match.values[i], param);
    }
    return std::format("{} is {}", name, values);
}

[[nodiscard]] std::vector<std::string> splitList(const std::string_view text) {
    std::vector<std::string> parts;
    usize start = 0;
    while (start <= text.size()) {
        const usize end = std::min(text.find(',', start), text.size());
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            parts.emplace_back(part);
        }
        start = end + 1;
    }
    return parts;
}

[[nodiscard]] std::optional<s64> parseNumber(std::string_view text) {
    const bool isHex = text.starts_with("0x") || text.starts_with("0X");
    if (isHex) {
        text.remove_prefix(2);
    }
    s64 value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value, isHex ? 16 : 10);
    return error == std::errc() && end == text.data() + text.size() ? std::optional(value)
                                                                    : std::nullopt;
}

[[nodiscard]] bool editChoices(std::vector<MatchValue>& values, const ActorParam& param) {
    bool isChanged = false;
    for (const ChoiceValue& choice : param.values) {
        const auto found = std::ranges::find(values, MatchValue(choice.value));
        bool isOn = found != values.end();
        const std::string label = std::format("{}  ({})", choice.label, choice.value);
        if (ImGui::Checkbox(label.c_str(), &isOn)) {
            if (isOn) {
                values.emplace_back(choice.value);
            } else {
                values.erase(found);
            }
            isChanged = true;
        }
    }
    return isChanged;
}

/// What is typed into the list of the popup that is open; only one is open at
/// a time, and it is kept while typing so a half-typed value is not lost.
std::string listText;

[[nodiscard]] bool editList(std::vector<MatchValue>& values, const bool isNumbers) {
    if (ImGui::IsWindowAppearing()) {
        listText.clear();
        for (const MatchValue& value : values) {
            listText += (listText.empty() ? "" : ", ") + describeValue(value, nullptr);
        }
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
    if (!ImGui::InputTextWithHint("##values", isNumbers ? "1, 2, 0x10" : "SCENE_DEKU_TREE, ...",
                                  &listText)) {
        return false;
    }
    values.clear();
    for (const std::string& part : splitList(listText)) {
        if (!isNumbers) {
            values.emplace_back(part);
        } else if (const std::optional<s64> number = parseNumber(part)) {
            values.emplace_back(*number);
        }
    }
    return true;
}

[[nodiscard]] bool editValues(ParamMatch& match, const ActorParam* param) {
    if (match.param == "age") {
        const bool isAdult =
            !match.values.empty() && describeValue(match.values.front(), nullptr) == "Adult";
        if (ImGui::RadioButton("Child", !isAdult) && isAdult) {
            match.values = {std::string("child")};
            return true;
        }
        if (ImGui::RadioButton("Adult", isAdult) && !isAdult) {
            match.values = {std::string("adult")};
            return true;
        }
        return false;
    }
    if (match.param == "scene" || param == nullptr) {
        return editList(match.values, param != nullptr);
    }
    if (param->kind == ParamKind::Flag) {
        const bool isSet = !match.values.empty() && match.values.front() == MatchValue(true);
        if (ImGui::RadioButton("Set", isSet) && !isSet) {
            match.values = {true};
            return true;
        }
        if (ImGui::RadioButton("Not set", !isSet) && isSet) {
            match.values = {false};
            return true;
        }
        return false;
    }
    if (param->kind == ParamKind::Choice && !param->values.empty()) {
        return editChoices(match.values, *param);
    }
    return editList(match.values, true);
}

[[nodiscard]] ParamMatch makeMatch(const std::string& name, const ActorParam* param) {
    if (name == "age") {
        return ParamMatch{.param = name, .values = {std::string("child")}};
    }
    if (name == "scene") {
        return ParamMatch{.param = name};
    }
    if (param != nullptr && param->kind == ParamKind::Flag) {
        return ParamMatch{.param = name, .values = {true}};
    }
    if (param != nullptr && !param->values.empty()) {
        return ParamMatch{.param = name, .values = {param->values.front().value}};
    }
    return ParamMatch{.param = name, .values = {s64{0}}};
}

[[nodiscard]] bool drawAddCondition(std::vector<ParamMatch>& match,
                                    const std::span<const ActorParam> params, Sentence& sentence) {
    return sentence.addToken("##add", "+ Condition", [&] {
        std::vector<std::string> names;
        for (const ActorParam& param : params) {
            names.push_back(param.name);
        }
        names.emplace_back("age");
        names.emplace_back("scene");
        for (const std::string& name : names) {
            if (std::ranges::contains(match, name, &ParamMatch::param)) {
                continue;
            }
            const std::string label = name == "age" ? "Age" : name == "scene" ? "Scene" : name;
            if (ImGui::Selectable(label.c_str())) {
                match.push_back(makeMatch(name, findParam(params, name)));
                return true;
            }
        }
        return false;
    });
}

} // namespace

bool drawConditionEditor(std::vector<ParamMatch>& match, const std::span<const ActorParam> params) {
    Sentence sentence;
    bool isChanged = false;
    std::optional<usize> removed;
    if (match.empty()) {
        sentence.addWords("Every placement");
    }
    for (usize i = 0; i < match.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ActorParam* param = findParam(params, match[i].param);
        isChanged = sentence.addToken("##match", describeMatch(match[i], param), [&] {
            return editValues(match[i], param);
        }) || isChanged;
        if (sentence.addButton(core::icons::X, "Remove this condition")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        match.erase(match.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    return drawAddCondition(match, params, sentence) || isChanged;
}

std::string describeCondition(const std::span<const ParamMatch> match,
                              const std::span<const ActorParam> params) {
    if (match.empty()) {
        return "Every placement";
    }
    std::string described;
    for (usize i = 0; i < match.size(); ++i) {
        described +=
            (i == 0 ? "" : ", and ") + describeMatch(match[i], findParam(params, match[i].param));
    }
    return described;
}

} // namespace toki::actor
