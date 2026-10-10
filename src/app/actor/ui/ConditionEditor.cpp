#include "ConditionEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ParamReading.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

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

constexpr float ChipValueShare = 0.7F;

[[nodiscard]] std::string describeParam(const std::string& name) {
    return name == "age" ? "Age" : name == "scene" ? "Scene" : name;
}

[[nodiscard]] std::string describeValues(const ParamMatch& match, const ActorParam* param) {
    if (match.values.empty()) {
        return "nothing";
    }
    std::string values;
    for (usize i = 0; i < match.values.size(); ++i) {
        values += (i == 0 ? "" : " or ") + describeValue(match.values[i], param);
    }
    return values;
}

[[nodiscard]] std::string describeMatch(const ParamMatch& match, const ActorParam* param) {
    return std::format("{} is {}", describeParam(match.param), describeValues(match, param));
}

class Flow {
public:
    void placeNext(const float width) {
        if (!isFirst) {
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < width) {
                ImGui::NewLine();
            }
        }
        isFirst = false;
    }

private:
    bool isFirst = true;
};

[[nodiscard]] bool drawChip(const std::string& key, const std::string& value,
                            const float maxValueWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string shown = makeFittedText(value, maxValueWidth);
    const float keyWidth = ImGui::CalcTextSize(key.c_str()).x;
    const float valueWidth = ImGui::CalcTextSize(shown.c_str()).x;
    const ImVec2 size(keyWidth + valueWidth + style.ItemInnerSpacing.x +
                          (style.FramePadding.x * 2.0F),
                      ImGui::GetFrameHeight());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool isClicked = ImGui::InvisibleButton("##chip", size);
    const ImVec4 background =
        ImGui::IsItemHovered() ? core::palette::SurfaceHovered : core::palette::SurfaceRaised;
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawList.AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y),
                           ImGui::GetColorU32(background), size.y * 0.5F);
    const float textY = start.y + style.FramePadding.y;
    const float keyX = start.x + style.FramePadding.x;
    drawList.AddText(ImVec2(keyX, textY), ImGui::GetColorU32(core::palette::TextMuted),
                     key.c_str());
    drawList.AddText(ImVec2(keyX + keyWidth + style.ItemInnerSpacing.x, textY),
                     ImGui::GetColorU32(core::palette::Warning), shown.c_str());
    if (shown != value) {
        ImGui::SetItemTooltip("%s is %s", key.c_str(), value.c_str());
    }
    return isClicked;
}

[[nodiscard]] float measureChip(const std::string& key, const std::string& value,
                                const float maxValueWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(key.c_str()).x +
           std::min(ImGui::CalcTextSize(value.c_str()).x, maxValueWidth) +
           style.ItemInnerSpacing.x + (style.FramePadding.x * 2.0F) + ImGui::GetFrameHeight();
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
        return editList(match.values, match.param != "scene");
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
                                    const std::span<const ActorParam> params, Flow& flow) {
    constexpr const char* AddLabel = "+ Condition";
    flow.placeNext(ImGui::CalcTextSize(AddLabel).x + (ImGui::GetStyle().FramePadding.x * 2.0F));
    if (ImGui::SmallButton(AddLabel)) {
        ImGui::OpenPopup("##add");
    }
    if (!ImGui::BeginPopup("##add")) {
        return false;
    }
    std::vector<std::string> names;
    for (const ActorParam& param : params) {
        names.push_back(param.name);
    }
    names.emplace_back("age");
    names.emplace_back("scene");
    bool isChanged = false;
    for (const std::string& name : names) {
        if (std::ranges::contains(match, name, &ParamMatch::param)) {
            continue;
        }
        if (ImGui::Selectable(describeParam(name).c_str())) {
            match.push_back(makeMatch(name, findParam(params, name)));
            isChanged = true;
        }
    }
    ImGui::EndPopup();
    return isChanged;
}

} // namespace

bool drawConditionEditor(std::vector<ParamMatch>& match, const std::span<const ActorParam> params) {
    Flow flow;
    bool isChanged = false;
    std::optional<usize> removed;
    const float maxValueWidth = ImGui::GetContentRegionAvail().x * ChipValueShare;
    if (match.empty()) {
        const char* every = "Every placement";
        flow.placeNext(ImGui::CalcTextSize(every).x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(core::palette::TextMuted, "%s", every);
    }
    for (usize i = 0; i < match.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ActorParam* param = findParam(params, match[i].param);
        const std::string key = describeParam(match[i].param);
        const std::string values = describeValues(match[i], param);
        flow.placeNext(measureChip(key, values, maxValueWidth));
        if (drawChip(key, values, maxValueWidth)) {
            ImGui::OpenPopup("##values");
        }
        ImGui::SameLine(0.0F, 0.0F);
        if (drawIconButton(core::icons::X, "Remove this condition")) {
            removed = i;
        }
        if (ImGui::BeginPopup("##values")) {
            isChanged = editValues(match[i], param) || isChanged;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removed) {
        match.erase(match.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    return drawAddCondition(match, params, flow) || isChanged;
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
