#include "EditWidgets.h"

#include "app/actor/Types.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace toki::actor {

namespace {

constexpr float PickerHeight = 260.0F;
constexpr float LabelWidth = 9.0F;
constexpr float MinimumTableWidth = 26.0F;

std::string pickerFilter;

[[nodiscard]] bool drawPicker(const char* id, std::string& typed,
                              std::span<const std::string> candidates, const char* emptyLabel) {
    bool isChanged = false;
    const std::string shown = typed.empty() && emptyLabel != nullptr ? emptyLabel : typed;
    if (!ImGui::BeginCombo(id, shown.c_str(), ImGuiComboFlags_HeightLargest)) {
        return false;
    }
    if (ImGui::IsWindowAppearing()) {
        pickerFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter, or type a name and press Enter", &pickerFilter);
    if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter) &&
        !pickerFilter.empty()) {
        typed = pickerFilter;
        isChanged = true;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::BeginChild("##candidates", ImVec2(0.0F, PickerHeight))) {
        if (emptyLabel != nullptr && ImGui::Selectable(emptyLabel, typed.empty())) {
            isChanged = !typed.empty();
            typed.clear();
            ImGui::CloseCurrentPopup();
        }
        for (const std::string& candidate : candidates) {
            if (!core::containsIgnoringCase(candidate, pickerFilter)) {
                continue;
            }
            if (ImGui::Selectable(candidate.c_str(), candidate == typed)) {
                isChanged = candidate != typed;
                typed = candidate;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndCombo();
    return isChanged;
}

[[nodiscard]] std::vector<std::string> listSymbols(const std::vector<object::ObjectAsset>& assets,
                                                   const std::span<const object::AssetKind> kinds) {
    std::vector<std::string> names;
    for (const object::ObjectAsset& asset : assets) {
        if (kinds.empty() || std::ranges::contains(kinds, asset.kind)) {
            names.push_back(asset.name);
        }
    }
    return names;
}

[[nodiscard]] bool drawSetBox(bool& isSet) {
    const bool isChanged = ImGui::Checkbox("##set", &isSet);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    return isChanged;
}

} // namespace

bool beginProperties(const char* id) {
    const float width =
        std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * MinimumTableWidth);
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp, ImVec2(width, 0.0F))) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * LabelWidth);
    ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void endProperties() {
    ImGui::EndTable();
}

void drawPropertyLabel(const std::string_view label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool editText(const char* label, std::string& text) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged = ImGui::InputText("##value", &text);
    ImGui::PopID();
    return isChanged;
}

bool editNotes(const char* label, std::string& text) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged = ImGui::InputTextMultiline(
        "##value", &text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0F));
    ImGui::PopID();
    return isChanged;
}

bool editOptionalNumber(const char* label, std::optional<f32>& value, const f32 shown) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    bool isSet = value.has_value();
    bool isChanged = drawSetBox(isSet);
    if (isChanged) {
        value = isSet ? std::optional(value.value_or(shown)) : std::nullopt;
    }
    ImGui::BeginDisabled(!isSet);
    f32 edited = value.value_or(shown);
    if (ImGui::InputFloat("##value", &edited, 0.0F, 0.0F, "%.6g") && isSet) {
        value = edited;
        isChanged = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return isChanged;
}

bool editVector(const char* label, std::array<f32, 3>& value) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged = ImGui::InputFloat3("##value", value.data(), "%.6g");
    ImGui::PopID();
    return isChanged;
}

bool editOptionalColor(const char* label, std::optional<std::array<u8, 4>>& color) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    bool isSet = color.has_value();
    bool isChanged = drawSetBox(isSet);
    if (isChanged) {
        color = isSet ? std::optional(color.value_or(std::array<u8, 4>{255, 255, 255, 255}))
                      : std::nullopt;
    }
    ImGui::BeginDisabled(!isSet);
    const std::array<u8, 4> current = color.value_or(std::array<u8, 4>{255, 255, 255, 255});
    std::array<f32, 4> edited{};
    for (usize i = 0; i < 4; ++i) {
        edited[i] = static_cast<f32>(current[i]) / 255.0F;
    }
    if (ImGui::ColorEdit4("##value", edited.data(), ImGuiColorEditFlags_Uint8) && isSet) {
        std::array<u8, 4> stored{};
        for (usize i = 0; i < 4; ++i) {
            stored[i] = static_cast<u8>((std::clamp(edited[i], 0.0F, 1.0F) * 255.0F) + 0.5F);
        }
        color = stored;
        isChanged = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return isChanged;
}

bool editObject(const char* label, std::optional<std::string>& object, const EditContext& context,
                const std::string_view inherited) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    std::string typed = object.value_or("");
    const std::string empty = std::format("{} (inherited)", inherited);
    const bool isChanged = drawPicker("##value", typed, context.objects, empty.c_str());
    ImGui::PopID();
    if (!isChanged) {
        return false;
    }
    object = typed.empty() ? std::nullopt : std::optional(typed);
    return true;
}

bool editSymbol(const char* label, std::string& symbol, const std::optional<std::string>& object,
                const std::span<const object::AssetKind> kinds, const EditContext& context) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const std::vector<object::ObjectAsset>* assets =
        object ? context.symbolCatalog.findSymbols(*object) : nullptr;
    const std::vector<std::string> names =
        assets != nullptr ? listSymbols(*assets, kinds) : std::vector<std::string>{};
    const bool isMissing = assets != nullptr && !symbol.empty() &&
                           !std::ranges::contains(*assets, symbol, &object::ObjectAsset::name);
    if (isMissing) {
        ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Warning);
    }
    const bool isChanged = drawPicker("##value", symbol, names, nullptr);
    if (isMissing) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s is not in %s", symbol.c_str(), object->c_str());
    } else if (object && assets == nullptr) {
        ImGui::SetItemTooltip("Reading %s", object->c_str());
    }
    ImGui::PopID();
    return isChanged;
}

bool editOptionalSymbol(const char* label, std::optional<std::string>& symbol,
                        const std::optional<std::string>& object,
                        const std::span<const object::AssetKind> kinds,
                        const EditContext& context) {
    std::string typed = symbol.value_or("");
    if (!editSymbol(label, typed, object, kinds, context)) {
        return false;
    }
    symbol = typed.empty() ? std::nullopt : std::optional(typed);
    return true;
}

bool editActor(const char* label, std::string& actor, const EditContext& context) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged = drawPicker("##value", actor, context.actors, nullptr);
    ImGui::PopID();
    return isChanged;
}

bool editCollection(const char* label, std::optional<std::string>& collection,
                    const std::span<const std::string> names) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    std::string typed = collection.value_or("");
    const bool isChanged = drawPicker("##value", typed, names, "Listed here");
    ImGui::PopID();
    if (!isChanged) {
        return false;
    }
    collection = typed.empty() ? std::nullopt : std::optional(typed);
    return true;
}

bool editSignedHex(const char* label, s64& value) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    std::string text = value < 0 ? std::format("-{:X}", -value) : std::format("{:X}", value);
    bool isChanged = false;
    if (ImGui::InputText("##value", &text, ImGuiInputTextFlags_CharsHexadecimal)) {
        const bool isNegative = text.starts_with('-');
        const std::string_view digits = std::string_view(text).substr(isNegative ? 1 : 0);
        s64 parsed = 0;
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
        if (digits.empty() || (error == std::errc() && end == digits.data() + digits.size())) {
            value = isNegative ? -parsed : parsed;
            isChanged = true;
        }
    }
    ImGui::SetItemTooltip("In hexadecimal");
    ImGui::PopID();
    return isChanged;
}

bool editHex(const char* label, u16& value) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged =
        ImGui::InputScalar("##value", ImGuiDataType_U16, &value, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopID();
    return isChanged;
}

bool drawIconButton(const char* icon, const char* tooltip) {
    const bool isClicked = ImGui::SmallButton(icon);
    ImGui::SetItemTooltip("%s", tooltip);
    return isClicked;
}

} // namespace toki::actor
