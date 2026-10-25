#include "CollisionRows.h"

#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize FilteredValues = 12;

[[nodiscard]] std::string makeCameraLabel(const Scene& scene, const Collision& collision,
                                          const usize index) {
    if (index >= collision.cameras.size()) {
        return std::format("{} (not in the list)", index);
    }
    const u16 setting = collision.cameras[index].setting;
    const std::vector<NamedValue>& names = scene.headerNames.cameraSettings;
    const auto named = std::ranges::find(names, s64{setting}, &NamedValue::value);
    return named != names.end() ? std::format("{} · {}", index, named->label)
                                : std::format("{} · setting {}", index, setting);
}

} // namespace

void CollisionRows::drawTitle(const char* icon, const std::string& name, const std::string& detail,
                              const usize selected) {
    ImGui::TextColored(core::palette::Accent, "%s", icon);
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
    ImGui::TextColored(core::palette::TextMuted, "%s", detail.c_str());
    if (selected > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           selected);
    }
    ImGui::Spacing();
}

bool CollisionRows::drawSharing(const Scene& scene, const usize layer) {
    const std::optional<usize> collision = scene.layers[layer].collision;
    std::string sharing;
    usize count = 0;
    for (const SceneLayer& other : scene.layers) {
        if (other.collision == collision) {
            sharing += std::format("{}{}", count == 0 ? "" : ", ", makeLayerName(other.index));
            ++count;
        }
    }
    if (count <= 1) {
        return false;
    }
    ImGui::TextColored(core::palette::Warning, "Shared by %zu layers", count);
    ImGui::SetItemTooltip("%s read this collision, and an edit of it changes it for each of them.",
                          sharing.c_str());
    ImGui::SameLine();
    const bool isClicked = ImGui::SmallButton("Make Own Copy for This Layer");
    ImGui::SetItemTooltip("Gives %s a collision of its own, copied from this one, so that its "
                          "edits leave the other layers as they are.",
                          makeLayerName(scene.layers[layer].index).c_str());
    ImGui::Spacing();
    return isClicked;
}

std::optional<u8> CollisionRows::drawCameraChoice(const Scene& scene, const Collision& collision,
                                                  const u8 index) {
    std::optional<u8> picked;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##camera", makeCameraLabel(scene, collision, index).c_str(),
                           ImGuiComboFlags_HeightLarge)) {
        return picked;
    }
    for (usize i = 0; i < collision.cameras.size() && i <= 0xFF; ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(makeCameraLabel(scene, collision, i).c_str(), i == index) &&
            i != index) {
            picked = static_cast<u8>(i);
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return picked;
}

std::optional<u16> CollisionRows::drawCameraSettingChoice(const Scene& scene, const u16 setting) {
    const std::vector<NamedValue>& names = scene.headerNames.cameraSettings;
    if (names.empty()) {
        if (const std::optional<s64> typed = drawNumber("##setting", setting, 0, 0xFFFF)) {
            return static_cast<u16>(*typed);
        }
        return std::nullopt;
    }
    if (const std::optional<s64> picked = drawNamedChoice("##setting", names, setting)) {
        return static_cast<u16>(*picked);
    }
    return std::nullopt;
}

std::optional<s64> CollisionRows::drawNamedChoice(const std::string_view id,
                                                  const std::span<const NamedValue> values,
                                                  const s64 value) {
    const auto current = std::ranges::find(values, value, &NamedValue::value);
    const std::string label =
        current != values.end() ? current->label : std::format("{} (unnamed)", value);
    std::optional<s64> picked;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo(std::string(id).c_str(), label.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (current != values.end()) {
            core::drawConstantName(current->label, current->name);
        }
        return picked;
    }
    const bool isFiltered = values.size() > FilteredValues;
    if (isFiltered) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter", &filter);
    }
    for (usize i = 0; i < values.size(); ++i) {
        const NamedValue& named = values[i];
        if (isFiltered && !core::isConstantFilteredIn(named.label, named.name, filter)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        if (core::drawConstantSelectable(named.label, named.name, named.value == value) &&
            named.value != value) {
            picked = named.value;
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return picked;
}

std::optional<bool> CollisionRows::drawToggle(const std::string_view id, const bool value,
                                              const bool isMixed) {
    bool isOn = value && !isMixed;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, isMixed);
    const bool isClicked = ImGui::Checkbox(std::string(id).c_str(), &isOn);
    ImGui::PopItemFlag();
    if (isClicked) {
        return isOn;
    }
    return std::nullopt;
}

} // namespace toki::scene
