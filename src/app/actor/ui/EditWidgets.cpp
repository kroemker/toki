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
#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

namespace {

constexpr f32 PickerHeight = 260.0F;

/// The filter of the picker that is open; only one is open at a time.
std::string pickerFilter;

/// A combo listing `candidates`, filtered by what is typed, with `typed` set
/// to the one picked. Returns whether one was.
[[nodiscard]] bool drawPicker(const char* label, std::string& typed,
                              std::span<const std::string> candidates, const char* emptyLabel) {
    bool isChanged = false;
    const std::string shown = typed.empty() && emptyLabel != nullptr ? emptyLabel : typed;
    if (!ImGui::BeginCombo(label, shown.c_str(), ImGuiComboFlags_HeightLargest)) {
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

} // namespace

bool editText(const char* label, std::string& text) {
    return ImGui::InputText(label, &text);
}

bool editNotes(const char* label, std::string& text) {
    return ImGui::InputTextMultiline(label, &text,
                                     ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.5F));
}

bool editOptionalText(const char* label, std::optional<std::string>& text) {
    std::string edited = text.value_or("");
    if (!ImGui::InputText(label, &edited)) {
        return false;
    }
    text = edited.empty() ? std::nullopt : std::optional(edited);
    return true;
}

bool editOptionalNumber(const char* label, std::optional<f32>& value, const f32 shown) {
    ImGui::PushID(label);
    bool isSet = value.has_value();
    bool isChanged = ImGui::Checkbox("##set", &isSet);
    if (isChanged) {
        value = isSet ? std::optional(value.value_or(shown)) : std::nullopt;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!isSet);
    f32 edited = value.value_or(shown);
    if (ImGui::DragFloat(label, &edited, 0.001F * std::max(1.0F, std::abs(edited)), 0.0F, 0.0F,
                         "%.4g") &&
        isSet) {
        value = edited;
        isChanged = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return isChanged;
}

bool editVector(const char* label, std::array<f32, 3>& value) {
    return ImGui::DragFloat3(label, value.data(), 1.0F, 0.0F, 0.0F, "%.4g");
}

bool editOptionalColor(const char* label, std::optional<std::array<u8, 4>>& color) {
    ImGui::PushID(label);
    bool isSet = color.has_value();
    bool isChanged = ImGui::Checkbox("##set", &isSet);
    if (isChanged) {
        color = isSet ? std::optional(color.value_or(std::array<u8, 4>{255, 255, 255, 255}))
                      : std::nullopt;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!isSet);
    const std::array<u8, 4> current = color.value_or(std::array<u8, 4>{255, 255, 255, 255});
    std::array<f32, 4> edited{};
    for (usize i = 0; i < 4; ++i) {
        edited[i] = static_cast<f32>(current[i]) / 255.0F;
    }
    if (ImGui::ColorEdit4(label, edited.data(), ImGuiColorEditFlags_Uint8) && isSet) {
        std::array<u8, 4> stored{};
        for (usize i = 0; i < 4; ++i) {
            stored[i] = static_cast<u8>(std::clamp(edited[i], 0.0F, 1.0F) * 255.0F + 0.5F);
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
    std::string typed = object.value_or("");
    const std::string empty = std::format("{} (inherited)", inherited);
    if (!drawPicker(label, typed, context.objects, empty.c_str())) {
        return false;
    }
    object = typed.empty() ? std::nullopt : std::optional(typed);
    return true;
}

bool editSymbol(const char* label, std::string& symbol, const std::optional<std::string>& object,
                const std::span<const object::AssetKind> kinds, const EditContext& context) {
    const std::vector<object::ObjectAsset>* assets =
        object ? context.symbolCatalog.findSymbols(*object) : nullptr;
    const std::vector<std::string> names =
        assets != nullptr ? listSymbols(*assets, kinds) : std::vector<std::string>{};
    const bool isChanged = drawPicker(label, symbol, names, nullptr);
    if (object && assets == nullptr) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::TextMuted, "reading %s…", object->c_str());
    } else if (assets != nullptr && !symbol.empty() &&
               !std::ranges::contains(*assets, symbol, &object::ObjectAsset::name)) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::Warning, "not in %s", object->c_str());
    }
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
    return drawPicker(label, actor, context.actors, nullptr);
}

bool editHex(const char* label, u16& value) {
    return ImGui::InputScalar(label, ImGuiDataType_U16, &value, nullptr, nullptr, "%04X",
                              ImGuiInputTextFlags_CharsHexadecimal);
}

bool drawIconButton(const char* icon, const char* tooltip) {
    const bool isClicked = ImGui::SmallButton(icon);
    ImGui::SetItemTooltip("%s", tooltip);
    return isClicked;
}

} // namespace toki::actor
