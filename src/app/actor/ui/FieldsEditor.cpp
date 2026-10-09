#include "FieldsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr u8 MaxBit = 15;

[[nodiscard]] std::string describeField(const ParamField& field) {
    const std::string bits =
        field.kind == FieldKind::Flag
            ? std::format("bit {}", field.shift)
            : std::format("bits {}..{}", field.shift, field.shift + field.width - 1);
    return std::format("{}  ·  {}, {} of {}", field.name.empty() ? "(unnamed)" : field.name,
                       getEntryName(FieldKindNames, field.kind), bits,
                       getEntryName(FieldSourceNames, field.source));
}

[[nodiscard]] bool editBits(ParamField& field) {
    bool isChanged = false;
    int shift = field.shift;
    if (ImGui::SliderInt("Shift", &shift, 0, MaxBit)) {
        field.shift = static_cast<u8>(shift);
        isChanged = true;
    }
    if (field.kind != FieldKind::Flag) {
        int width = field.width;
        if (ImGui::SliderInt("Width", &width, 1, MaxBit + 1 - field.shift)) {
            field.width = static_cast<u8>(width);
            isChanged = true;
        }
        isChanged = ImGui::Checkbox("Signed", &field.isSigned) || isChanged;
        ImGui::SameLine();
        isChanged = ImGui::Checkbox("Hexadecimal", &field.isHex) || isChanged;
    } else {
        field.width = 1;
    }
    return isChanged;
}

[[nodiscard]] bool editValues(std::vector<EnumValue>& values) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < values.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0F);
        isChanged = ImGui::InputScalar("##value", ImGuiDataType_S64, &values[i].value) || isChanged;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
        isChanged = editText("##name", values[i].name) || isChanged;
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove this value")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Add a value")) {
        const s64 next = values.empty() ? 0 : values.back().value + 1;
        values.push_back(EnumValue{.name = std::format("value_{}", next), .value = next});
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editField(ParamField& field) {
    bool isChanged = editText("Name", field.name);
    isChanged = editName("Kind", field.kind, FieldKindNames) || isChanged;
    isChanged = editName("Source", field.source, FieldSourceNames) || isChanged;
    isChanged = editBits(field) || isChanged;
    if (field.kind != FieldKind::Flag) {
        isChanged = editName("Role", field.role, FieldRoleNames) || isChanged;
    }
    isChanged = editText("Meaning", field.meaning) || isChanged;
    if (field.kind == FieldKind::Enum) {
        ImGui::TextColored(core::palette::TextMuted, "Values");
        ImGui::Indent();
        isChanged = editValues(field.values) || isChanged;
        ImGui::Unindent();
    }
    return isChanged;
}

} // namespace

bool drawFieldsEditor(std::vector<ParamField>& fields) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < fields.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("{}###field", describeField(fields[i]));
        const bool isOpen = ImGui::TreeNode(label.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
                        ImGui::GetFrameHeight());
        if (drawIconButton(core::icons::Trash, "Remove this field")) {
            removed = i;
        }
        if (isOpen) {
            isChanged = editField(fields[i]) || isChanged;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removed) {
        fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::Button("Add a field")) {
        fields.push_back(ParamField{.name = std::format("field_{}", fields.size())});
        isChanged = true;
    }
    return isChanged;
}

} // namespace toki::actor
