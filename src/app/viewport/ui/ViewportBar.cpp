#include "ViewportBar.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportView.h"

#include <imgui.h>

#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr const char* ShadingPopup = "##shading";
constexpr const char* OverlaysPopup = "##overlays";
constexpr float LabelWidth = 9.0F;
constexpr auto ActorDisplayLabels = std::to_array<const char*>({"Hidden", "Markers", "Models"});

[[nodiscard]] float findButtonWidth(const std::string& label) {
    return ImGui::CalcTextSize(label.c_str(), nullptr, true).x +
           (ImGui::GetStyle().FramePadding.x * 2.0F);
}

/// A button drawn as pressed while `isActive`, as a switch that is on.
[[nodiscard]] bool drawToggleButton(const std::string& label, const bool isActive,
                                    const char* tooltip) {
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, core::palette::AccentMuted);
    }
    const bool clicked = ImGui::Button(label.c_str());
    if (isActive) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

/// A button that opens `popup` under it, lined up with its left edge or, with
/// `alignsRight`, its right one. True while the popup is open, which must then
/// be ended.
[[nodiscard]] bool beginPopover(const std::string& label, const char* popup,
                                const bool alignsRight) {
    const bool isOpen = ImGui::IsPopupOpen(popup);
    if (isOpen) {
        ImGui::PushStyleColor(ImGuiCol_Border, core::palette::Accent);
    }
    if (ImGui::Button(label.c_str())) {
        ImGui::OpenPopup(popup);
    }
    if (isOpen) {
        ImGui::PopStyleColor();
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float below = max.y + core::space::Small;
    ImGui::SetNextWindowPos(alignsRight ? ImVec2(max.x, below) : ImVec2(min.x, below),
                            ImGuiCond_Always,
                            alignsRight ? ImVec2(1.0F, 0.0F) : ImVec2(0.0F, 0.0F));
    return ImGui::BeginPopup(popup);
}

template <typename Enum, usize Count>
[[nodiscard]] std::optional<Enum>
drawEnumCombo(const char* id, const std::array<const char*, Count>& labels, const Enum value) {
    std::optional<Enum> picked;
    if (ImGui::BeginCombo(id, labels[static_cast<usize>(value)])) {
        for (usize i = 0; i < labels.size(); ++i) {
            if (ImGui::Selectable(labels[i], i == static_cast<usize>(value))) {
                picked = static_cast<Enum>(i);
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

[[nodiscard]] std::optional<usize>
drawNameCombo(const char* id, const std::vector<std::string>& names, const usize picked) {
    std::optional<usize> chosen;
    const char* preview = picked < names.size() ? names[picked].c_str() : "";
    if (ImGui::BeginCombo(id, preview)) {
        for (usize i = 0; i < names.size(); ++i) {
            if (ImGui::Selectable(names[i].c_str(), i == picked)) {
                chosen = i;
            }
        }
        ImGui::EndCombo();
    }
    return chosen;
}

[[nodiscard]] ViewportBarEdit drawShadingForm(const std::optional<ViewportChoices>& choices) {
    ViewportBarEdit edit;
    if (!core::beginProperties("##shadingForm", LabelWidth)) {
        return edit;
    }
    if (choices && !choices->lightSettings.empty()) {
        core::drawPropertyLabel("Light Setting", "The scene's environment light setting: its "
                                                 "ambient colour, directional lights and fog.");
        if (const std::optional<usize> chosen =
                drawNameCombo("##lightSetting", choices->lightSettings, choices->lightSetting)) {
            ViewportChoices changed = *choices;
            changed.lightSetting = *chosen;
            edit.choices = std::move(changed);
        }
    }
    core::endProperties();
    return edit;
}

[[nodiscard]] ViewportBarEdit drawOverlaysForm(const ViewportView& view,
                                               const std::optional<ViewportChoices>& choices) {
    ViewportBarEdit edit;
    if (!core::beginProperties("##overlaysForm", LabelWidth)) {
        return edit;
    }
    if (choices && choices->actors) {
        core::drawPropertyLabel("Actors", "Models draws each actor as its database entry says; "
                                          "an actor without an entry stays a marker.");
        if (const std::optional<ActorDisplay> picked =
                drawEnumCombo("##actors", ActorDisplayLabels, *choices->actors)) {
            ViewportChoices changed = *choices;
            changed.actors = *picked;
            edit.choices = std::move(changed);
        }
    }
    ViewportView changed = view;
    core::drawPropertyLabel("Flag Links", "A line from each actor that sets a flag to each that "
                                          "reads it; hover a flag to see only its own.");
    if (ImGui::Checkbox("##flagLinks", &changed.overlays.flagLinks)) {
        edit.view = changed;
    }
    core::endProperties();
    return edit;
}

[[nodiscard]] usize countShownOverlays(const ViewportView& view,
                                       const std::optional<ViewportChoices>& choices) {
    usize count = view.overlays.flagLinks ? 1 : 0;
    if (choices && choices->actors && *choices->actors != ActorDisplay::Hidden) {
        ++count;
    }
    return count;
}

} // namespace

ViewportBarEdit drawViewportBar(const ViewportView& view,
                                const std::optional<ViewportChoices>& choices) {
    ViewportBarEdit edit;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const std::string shading = std::format("{} Shading", core::icons::SlidersHorizontal);
    if (choices && !choices->lightSettings.empty() && beginPopover(shading, ShadingPopup, false)) {
        edit = drawShadingForm(choices);
        ImGui::EndPopup();
    }

    const std::string toggle = view.overlays.isShown ? core::icons::Eye : core::icons::EyeSlash;
    const std::string overlays =
        std::format("Overlays {}###overlaysButton", countShownOverlays(view, choices));
    const float width =
        findButtonWidth(toggle) + ImGui::GetStyle().ItemSpacing.x + findButtonWidth(overlays);
    ImGui::SetCursorScreenPos(ImVec2(start.x + available - width, start.y));
    if (drawToggleButton(toggle, !view.overlays.isShown,
                         view.overlays.isShown ? "Hide all overlays (Shift+Alt+Z)"
                                               : "Show all overlays (Shift+Alt+Z)")) {
        ViewportView changed = view;
        changed.overlays.isShown = !changed.overlays.isShown;
        edit.view = changed;
    }
    ImGui::SameLine();
    if (beginPopover(overlays, OverlaysPopup, true)) {
        const ViewportBarEdit form = drawOverlaysForm(edit.view.value_or(view), choices);
        if (form.view) {
            edit.view = form.view;
        }
        if (form.choices) {
            edit.choices = form.choices;
        }
        ImGui::EndPopup();
    }
    return edit;
}

std::optional<ViewportView> applyViewportHotkeys(const ViewportView& view) {
    if (ImGui::GetIO().WantTextInput) {
        return std::nullopt;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_Z)) {
        ViewportView changed = view;
        changed.overlays.isShown = !changed.overlays.isShown;
        return changed;
    }
    return std::nullopt;
}

} // namespace toki::viewport
