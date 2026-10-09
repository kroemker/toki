#include "MatchEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] std::string describeValue(const MatchValue& value) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    if (const s64* number = std::get_if<s64>(&value)) {
        return std::format("{}", *number);
    }
    return std::get<std::string>(value);
}

[[nodiscard]] std::string joinValues(std::span<const MatchValue> values) {
    std::string joined;
    for (const MatchValue& value : values) {
        joined += (joined.empty() ? "" : ", ") + describeValue(value);
    }
    return joined;
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

[[nodiscard]] std::vector<MatchValue> parseNumbers(const std::string_view text) {
    std::vector<MatchValue> values;
    for (const std::string& part : splitList(text)) {
        s64 value = 0;
        const bool isHex = part.starts_with("0x") || part.starts_with("0X");
        const char* begin = part.data() + (isHex ? 2 : 0);
        const auto [end, error] =
            std::from_chars(begin, part.data() + part.size(), value, isHex ? 16 : 10);
        if (error == std::errc() && end == part.data() + part.size()) {
            values.emplace_back(value);
        }
    }
    return values;
}

[[nodiscard]] bool isSelected(std::span<const MatchValue> values, const EnumValue& value) {
    return std::ranges::any_of(values, [&value](const MatchValue& matched) {
        const std::string* name = std::get_if<std::string>(&matched);
        const s64* number = std::get_if<s64>(&matched);
        return (name != nullptr && *name == value.name) ||
               (number != nullptr && *number == value.value);
    });
}

[[nodiscard]] bool editEnumValues(std::vector<MatchValue>& values, const ParamField& field) {
    bool isChanged = false;
    for (const EnumValue& value : field.values) {
        bool isOn = isSelected(values, value);
        if (ImGui::Checkbox(value.name.c_str(), &isOn)) {
            std::erase_if(values, [&value](const MatchValue& matched) {
                const std::string* name = std::get_if<std::string>(&matched);
                const s64* number = std::get_if<s64>(&matched);
                return (name != nullptr && *name == value.name) ||
                       (number != nullptr && *number == value.value);
            });
            if (isOn) {
                values.emplace_back(value.name);
            }
            isChanged = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    return isChanged;
}

[[nodiscard]] bool editTextValues(std::vector<MatchValue>& values, const bool isNumbers) {
    std::string text = joinValues(values);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::InputTextWithHint("##values", isNumbers ? "1, 2, 0x10" : "SCENE_DEKU_TREE, ...",
                                  &text)) {
        return false;
    }
    if (isNumbers) {
        values = parseNumbers(text);
    } else {
        values.clear();
        for (std::string& part : splitList(text)) {
            values.emplace_back(std::move(part));
        }
    }
    return true;
}

[[nodiscard]] MatchValue makeFirstValue(const ParamField* field, const std::string_view name) {
    if (name == "age") {
        return std::string("child");
    }
    if (field != nullptr && field->kind == FieldKind::Flag) {
        return true;
    }
    if (field != nullptr && field->kind == FieldKind::Enum && !field->values.empty()) {
        return field->values.front().name;
    }
    return s64{0};
}

[[nodiscard]] bool editValues(std::vector<MatchValue>& values, const ParamField* field,
                              const std::string_view name) {
    if (name == "age") {
        const bool isAdult = !values.empty() && describeValue(values.front()) == "adult";
        const bool isChildPicked = ImGui::RadioButton("child", !isAdult);
        ImGui::SameLine();
        const bool isAdultPicked = ImGui::RadioButton("adult", isAdult);
        if (isChildPicked == isAdultPicked || isAdultPicked == isAdult) {
            return false;
        }
        values = {std::string(isAdultPicked ? "adult" : "child")};
        return true;
    }
    if (field != nullptr && field->kind == FieldKind::Flag) {
        bool isSet = !values.empty() && std::holds_alternative<bool>(values.front()) &&
                     std::get<bool>(values.front());
        if (ImGui::Checkbox("set", &isSet)) {
            values = {isSet};
            return true;
        }
        return false;
    }
    if (field != nullptr && field->kind == FieldKind::Enum && !field->values.empty()) {
        return editEnumValues(values, *field);
    }
    return editTextValues(values, field != nullptr);
}

[[nodiscard]] bool editMatchedField(std::vector<FieldMatch>& match, const std::string& name,
                                    const ParamField* field) {
    ImGui::PushID(name.c_str());
    const auto found = std::ranges::find(match, name, &FieldMatch::field);
    bool isMatched = found != match.end();
    bool isChanged = false;
    if (ImGui::Checkbox(name.c_str(), &isMatched)) {
        if (isMatched) {
            match.push_back(FieldMatch{.field = name, .values = {makeFirstValue(field, name)}});
        } else {
            match.erase(found);
        }
        isChanged = true;
    } else if (isMatched) {
        ImGui::SameLine();
        ImGui::Indent(ImGui::GetFontSize() * 8.0F);
        isChanged = editValues(found->values, field, name);
        ImGui::Unindent(ImGui::GetFontSize() * 8.0F);
    }
    ImGui::PopID();
    return isChanged;
}

} // namespace

bool drawMatchEditor(std::vector<FieldMatch>& match, const std::span<const ParamField> fields) {
    bool isChanged = false;
    for (const ParamField& field : fields) {
        isChanged = editMatchedField(match, field.name, &field) || isChanged;
    }
    isChanged = editMatchedField(match, "age", nullptr) || isChanged;
    isChanged = editMatchedField(match, "scene", nullptr) || isChanged;
    return isChanged;
}

std::string describeMatch(const std::span<const FieldMatch> match) {
    if (match.empty()) {
        return "every placement";
    }
    std::string described;
    for (const FieldMatch& field : match) {
        described += std::format("{}{} = {}", described.empty() ? "" : "  ·  ", field.field,
                                 joinValues(field.values));
    }
    return described;
}

} // namespace toki::actor
