#include "ParamsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

namespace {

constexpr u8 BitCount = 16;
constexpr std::array ParamColors{
    ImVec4{0.353F, 0.639F, 0.949F, 1.0F}, ImVec4{0.361F, 0.788F, 0.478F, 1.0F},
    ImVec4{0.910F, 0.718F, 0.314F, 1.0F}, ImVec4{0.835F, 0.443F, 0.808F, 1.0F},
    ImVec4{0.949F, 0.408F, 0.369F, 1.0F}, ImVec4{0.380F, 0.808F, 0.808F, 1.0F},
    ImVec4{0.682F, 0.561F, 0.949F, 1.0F}, ImVec4{0.784F, 0.784F, 0.451F, 1.0F},
};

[[nodiscard]] ImVec4 findParamColor(const usize index) {
    return ParamColors[index % ParamColors.size()];
}

[[nodiscard]] ImVec4 makeDimmed(const ImVec4 color) {
    return ImVec4(color.x * 0.55F, color.y * 0.55F, color.z * 0.55F, 1.0F);
}

[[nodiscard]] std::string describeBits(const ActorParam& param) {
    if (param.mask == 0) {
        return "no bits yet: click the bits above";
    }
    const u8 shift = findMaskShift(param.mask);
    const u8 width = findMaskWidth(param.mask);
    const std::string bits = width == 1 ? std::format("bit {}", shift)
                                        : std::format("bits {} to {}", shift, shift + width - 1);
    return std::format("{}  ·  mask 0x{:04X}", bits, param.mask);
}

[[nodiscard]] std::optional<std::string>
findNameProblem(const ActorEntry& entry, const ActorParam& param, const std::string& name) {
    if (name.empty()) {
        return "A param needs a name";
    }
    if (isBuiltInParam(name)) {
        return std::format("'{}' is built in", name);
    }
    if (name != param.name && std::ranges::contains(entry.params, name, &ActorParam::name)) {
        return std::format("Another param is named '{}'", name);
    }
    return std::nullopt;
}

[[nodiscard]] bool isValueTaken(const ActorParam& param, const s64 value, const usize except) {
    for (usize i = 0; i < param.values.size(); ++i) {
        if (i != except && param.values[i].value == value) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string makeNewName(const ActorEntry& entry) {
    for (usize i = 1;; ++i) {
        std::string name = std::format("Param {}", i);
        if (!std::ranges::contains(entry.params, name, &ActorParam::name)) {
            return name;
        }
    }
}

} // namespace

std::optional<usize> ParamsEditor::findSelected(const ActorEntry& entry) const {
    if (!selected) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(entry.params, *selected, &ActorParam::name);
    return found == entry.params.end()
               ? std::nullopt
               : std::optional(static_cast<usize>(found - entry.params.begin()));
}

void ParamsEditor::select(const ActorEntry& entry, const std::optional<usize> index) {
    selected = index ? std::optional(entry.params[*index].name) : std::nullopt;
    isNameEdited = false;
}

bool ParamsEditor::draw(ActorEntry& entry, const std::span<const std::string> collections) {
    if (entry.actor != editedActor) {
        editedActor = entry.actor;
        select(entry, std::nullopt);
    }
    const std::optional<usize> selectedIndex = findSelected(entry);
    bool isChanged = false;
    std::vector<ParamSource> sources{ParamSource::Params};
    for (const ActorParam& param : entry.params) {
        if (!std::ranges::contains(sources, param.source)) {
            sources.push_back(param.source);
        }
    }
    if (selectedIndex && !std::ranges::contains(sources, entry.params[*selectedIndex].source)) {
        sources.push_back(entry.params[*selectedIndex].source);
    }
    std::ranges::sort(sources);
    for (const ParamSource source : sources) {
        isChanged = drawStrip(entry, source) || isChanged;
    }
    return drawList(entry, collections) || isChanged;
}

bool ParamsEditor::drawStrip(ActorEntry& entry, const ParamSource source) {
    ImGui::PushID("strip");
    ImGui::PushID(static_cast<int>(source));
    ImGui::AlignTextToFramePadding();
    const std::string_view label = getEntryLabel(ParamSourceNames, source);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(ImGui::GetFontSize() * 6.0F);
    bool isChanged = false;
    for (u8 bit = BitCount; bit-- > 0;) {
        isChanged = drawBit(entry, source, bit) || isChanged;
        if (bit > 0) {
            ImGui::SameLine(0.0F, bit % 4 == 0 ? core::space::Large : core::space::Tiny);
        }
    }
    ImGui::PopID();
    ImGui::PopID();
    return isChanged;
}

bool ParamsEditor::drawBit(ActorEntry& entry, const ParamSource source, const u8 bit) {
    const std::optional<usize> selectedIndex = findSelected(entry);
    const std::optional<usize> owner = findBitOwner(entry.params, source, bit);
    const bool isSelectable = selectedIndex && entry.params[*selectedIndex].source == source;
    const bool isMine = owner && owner == selectedIndex;
    const bool isTaken = owner && !isMine;
    const ImVec4 color = owner ? findParamColor(*owner) : core::palette::SurfaceRaised;
    ImGui::PushID(bit);
    ImGui::PushStyleColor(ImGuiCol_Button, isMine || !selectedIndex ? color : makeDimmed(color));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, owner ? color : core::palette::SurfaceHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, owner ? color : core::palette::SurfaceActive);
    ImGui::BeginDisabled(isTaken || !isSelectable);
    const std::string text = std::to_string(bit);
    const bool isClicked = ImGui::Button(
        text.c_str(), ImVec2(ImGui::GetFrameHeight() * 1.2F, ImGui::GetFrameHeight()));
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s",
                          owner ? std::format("Bit {}: {}", bit, entry.params[*owner].name).c_str()
                                : std::format("Bit {}: free", bit).c_str());
    }
    ImGui::PopID();
    if (!isClicked) {
        return false;
    }
    entry.params[*selectedIndex].mask ^= static_cast<u16>(1U << bit);
    return true;
}

bool ParamsEditor::drawList(ActorEntry& entry, const std::span<const std::string> collections) {
    const std::optional<usize> selectedIndex = findSelected(entry);
    bool isChanged = false;
    std::optional<usize> removed;
    ImGui::PushID("list");
    for (usize i = 0; i < entry.params.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::ColorButton("##color", findParamColor(i), ImGuiColorEditFlags_NoTooltip);
        ImGui::SameLine();
        const std::string label = std::format("{}  ·  {}, {}", entry.params[i].name,
                                              getEntryLabel(ParamKindNames, entry.params[i].kind),
                                              describeBits(entry.params[i]));
        if (ImGui::Selectable(label.c_str(), selectedIndex == i,
                              ImGuiSelectableFlags_AllowOverlap)) {
            select(entry, selectedIndex == i ? std::nullopt : std::optional(i));
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
                        ImGui::GetFrameHeight());
        if (drawIconButton(core::icons::Trash, "Remove this param")) {
            removed = i;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    if (removed) {
        entry.params.erase(entry.params.begin() + static_cast<std::ptrdiff_t>(*removed));
        select(entry, std::nullopt);
        isChanged = true;
    }
    if (ImGui::Button("Add a param")) {
        entry.params.push_back(ActorParam{.name = makeNewName(entry)});
        select(entry, entry.params.size() - 1);
        isChanged = true;
    }
    if (const std::optional<usize> edited = findSelected(entry)) {
        ImGui::SeparatorText(entry.params[*edited].name.c_str());
        isChanged = drawParam(entry, entry.params[*edited], collections) || isChanged;
    }
    return isChanged;
}

bool ParamsEditor::drawParam(ActorEntry& entry, ActorParam& param,
                             const std::span<const std::string> collections) {
    if (!beginProperties("param")) {
        return false;
    }
    bool isChanged = false;
    if (!isNameEdited) {
        nameText = param.name;
    }
    drawPropertyLabel("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool isNameChanged = ImGui::InputText("##name", &nameText);
    isNameEdited = ImGui::IsItemActive();
    const std::optional<std::string> nameProblem = findNameProblem(entry, param, nameText);
    if (isNameChanged && !nameProblem && nameText != param.name) {
        const std::string name = nameText;
        renameParam(entry, param.name, name);
        selected = name;
        isChanged = true;
    }
    if (nameProblem && nameText != param.name) {
        drawPropertyLabel("");
        ImGui::TextColored(core::palette::Warning, "%s %s", core::icons::Warning,
                           nameProblem->c_str());
    }
    isChanged = editName("Kind", param.kind, ParamKindNames) || isChanged;
    const ParamSource source = param.source;
    if (editName("Read from", param.source, ParamSourceNames)) {
        param.mask = source == param.source ? param.mask : u16{0};
        isChanged = true;
    }
    drawPropertyLabel("Bits");
    ImGui::TextUnformatted(describeBits(param).c_str());
    if (param.mask != 0 && !isMaskContiguous(param.mask)) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::Warning, "%s not next to each other",
                           core::icons::Warning);
    }
    if (param.kind == ParamKind::Choice) {
        isChanged = editCollection("Values", param.collection, collections) || isChanged;
        ImGui::SetItemTooltip("Listed in the entry, or taken from the checkout, so they follow it");
        isChanged = editSignedHex("Offset", param.offset) || isChanged;
        ImGui::SetItemTooltip("Added to the param's value before it is looked up, as a sign "
                              "adds 0x300 to its message");
    }
    if (param.kind == ParamKind::Number) {
        drawPropertyLabel("Shown");
        isChanged = ImGui::Checkbox("Signed", &param.isSigned) || isChanged;
        ImGui::SameLine();
        isChanged = ImGui::Checkbox("Hexadecimal", &param.isHex) || isChanged;
    }
    isChanged = editText("Description", param.description) || isChanged;
    endProperties();
    if (param.kind == ParamKind::Choice && !param.collection) {
        isChanged = drawChoices(param) || isChanged;
    }
    return isChanged;
}

bool ParamsEditor::drawChoices(ActorParam& param) {
    bool isChanged = false;
    std::optional<usize> removed;
    if (ImGui::BeginTable("choices", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 5.0F);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight());
        for (usize i = 0; i < param.values.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            s64 value = param.values[i].value;
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &value) &&
                !isValueTaken(param, value, i)) {
                param.values[i].value = value;
                isChanged = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            isChanged = ImGui::InputText("##label", &param.values[i].label) || isChanged;
            ImGui::TableNextColumn();
            if (drawIconButton(core::icons::Trash, "Remove this value")) {
                removed = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed) {
        param.values.erase(param.values.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Add a value")) {
        const s64 next = param.values.empty()
                             ? 0
                             : std::ranges::max(param.values, {}, &ChoiceValue::value).value + 1;
        param.values.push_back(ChoiceValue{.value = next, .label = std::format("Value {}", next)});
        isChanged = true;
    }
    return isChanged;
}

} // namespace toki::actor
