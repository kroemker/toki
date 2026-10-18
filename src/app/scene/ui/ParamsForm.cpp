#include "ParamsForm.h"

#include "app/actor/domain/ParamReading.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

/// How many values a choice may have before its dropdown gets a filter.
constexpr usize ShortList = 12;

[[nodiscard]] std::string_view getSourceName(const actor::ParamSource source) {
    switch (source) {
    case actor::ParamSource::Params:
        return "params";
    case actor::ParamSource::RotationX:
        return "rotation X";
    case actor::ParamSource::RotationY:
        return "rotation Y";
    case actor::ParamSource::RotationZ:
        return "rotation Z";
    }
    return "params";
}

[[nodiscard]] std::string_view getSceneFlagName(const actor::SceneFlag flag) {
    switch (flag) {
    case actor::SceneFlag::Switch:
        return "switch";
    case actor::SceneFlag::Treasure:
        return "treasure";
    case actor::SceneFlag::Collectible:
        return "collectible";
    case actor::SceneFlag::Clear:
        return "clear";
    }
    return "switch";
}

[[nodiscard]] std::string_view getFlagUseWord(const actor::FlagUse use) {
    switch (use) {
    case actor::FlagUse::Reads:
        return "reads";
    case actor::FlagUse::Sets:
        return "sets";
    case actor::FlagUse::Both:
        return "reads and sets";
    }
    return "reads";
}

[[nodiscard]] std::string makeHelp(const actor::ActorParam& param) {
    std::string help = param.description;
    if (!help.empty()) {
        help += "\n\n";
    }
    help += std::format("Bits 0x{:04X} of the {}.", param.mask, getSourceName(param.source));
    if (param.sceneFlag) {
        help += std::format(" A {} flag of the scene, which the actor {}.",
                            getSceneFlagName(*param.sceneFlag), getFlagUseWord(param.flagUse));
    }
    return help;
}

[[nodiscard]] bool isFilteredIn(const std::string_view label, const std::string_view filter) {
    const auto lower = [](const char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    };
    return filter.empty() ||
           std::ranges::search(label, filter, {}, lower, lower).begin() != label.end();
}

[[nodiscard]] bool editNumber(const actor::ActorParam& param, s64& value) {
    const actor::ParamRange range = actor::findParamRange(param);
    const bool isHex = param.isHex && !param.isSigned;
    ImGui::InputScalar("##value", ImGuiDataType_S64, &value, nullptr, nullptr,
                       isHex ? "%llX" : "%lld",
                       isHex ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_None);
    value = std::clamp(value, range.lowest, range.highest);
    return ImGui::IsItemDeactivatedAfterEdit();
}

} // namespace

bool ParamsForm::draw(const actor::ActorEntry& entry, actor::PlacementValues& placement,
                      const actor::FindCollection& findCollection) {
    bool isDone = false;
    for (const actor::ActorParam& param : entry.params) {
        ImGui::PushID(param.name.c_str());
        isDone = drawParam(param, placement, actor::findParamCollection(param, findCollection)) ||
                 isDone;
        ImGui::PopID();
    }
    const u16 unnamed = actor::findUnnamedBits(entry, actor::ParamSource::Params);
    if ((placement.params & unnamed) == 0) {
        return isDone;
    }
    core::drawPropertyLabel("Other Bits",
                            std::format("Bits of the params no param of the actor database "
                                        "names, 0x{:04X}, that this placement sets.",
                                        unnamed));
    auto bits = static_cast<u16>(placement.params & unnamed);
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Warning);
    ImGui::InputScalar("##otherBits", ImGuiDataType_U16, &bits, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopStyleColor();
    placement.params = static_cast<u16>((placement.params & ~unnamed) | (bits & unnamed));
    return ImGui::IsItemDeactivatedAfterEdit() || isDone;
}

bool ParamsForm::drawParam(const actor::ActorParam& param, actor::PlacementValues& placement,
                           const actor::ValueCollection* collection) {
    core::drawPropertyLabel(param.name, makeHelp(param));
    s64 value = actor::readParamValue(param, placement);
    bool isDone = false;
    switch (param.kind) {
    case actor::ParamKind::Flag: {
        bool isSet = value != 0;
        isDone = ImGui::Checkbox("##value", &isSet);
        value = isSet ? 1 : 0;
        break;
    }
    case actor::ParamKind::Number:
        isDone = editNumber(param, value);
        break;
    case actor::ParamKind::Choice:
        isDone = drawChoice(param, value, collection);
        break;
    }
    placement = actor::writeParamValue(param, placement, value);
    return isDone;
}

bool ParamsForm::drawChoice(const actor::ActorParam& param, s64& value,
                            const actor::ValueCollection* collection) {
    if (param.collection && collection == nullptr) {
        return editNumber(param, value);
    }
    const std::string current = actor::makeParamText(param, value, collection);
    if (!ImGui::BeginCombo("##value", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        return false;
    }
    const std::vector<actor::ChoiceValue>& values =
        collection != nullptr ? collection->values : param.values;
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        if (values.size() > ShortList) {
            ImGui::SetKeyboardFocusHere();
        }
    }
    if (values.size() > ShortList) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter", &filter);
    }
    const actor::ParamRange range = actor::findParamRange(param);
    bool isPicked = false;
    for (const actor::ChoiceValue& choice : values) {
        const s64 stored = choice.value - param.offset;
        if (stored < range.lowest || stored > range.highest ||
            !isFilteredIn(choice.label, filter)) {
            continue;
        }
        const std::string label = std::format("{}##{}", choice.label, choice.value);
        if (ImGui::Selectable(label.c_str(), stored == value)) {
            value = stored;
            isPicked = true;
        }
        if (stored == value && ImGui::IsWindowAppearing()) {
            ImGui::SetScrollHereY();
        }
    }
    ImGui::EndCombo();
    return isPicked;
}

} // namespace toki::scene
