#include "ViewportBar.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/RenderMode.h"
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
constexpr auto TextureFilterLabels = std::to_array<const char*>({"N64", "Point"});
constexpr std::array<const char*, ShadingCount> ShadingIcons{
    core::icons::ImageSquare, core::icons::Sphere, core::icons::Checkerboard, core::icons::Compass,
    core::icons::Gradient};

static_assert(TextureFilterLabels.size() == TextureFilterCount,
              "a texture filter was added to the enum without a label");

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

[[nodiscard]] bool drawSwitchRow(const char* label, const char* help, bool& value) {
    core::drawPropertyLabel(label, help);
    ImGui::PushID(label);
    const bool changed = ImGui::Checkbox("##switch", &value);
    ImGui::PopID();
    return changed;
}

[[nodiscard]] std::optional<RenderMode> drawPipelineRows(const RenderMode& mode) {
    RenderMode changed = mode;
    bool isChanged = drawSwitchRow("Lighting",
                                   "Directional and ambient lights on geometry that enables "
                                   "G_LIGHTING. Off, it is shaded white.",
                                   changed.lighting);
    isChanged |=
        drawSwitchRow("Fog", "The blender's fog, with the light setting's colour.", changed.fog);
    isChanged |= drawSwitchRow("Backface Culling",
                               "Culling as the geometry mode asks. Off, both sides of every "
                               "triangle are drawn.",
                               changed.culling);
    isChanged |= drawSwitchRow("Alpha Cutout",
                               "The alpha compare. Off, fences and foliage are drawn as solid "
                               "quads.",
                               changed.alphaCutout);
    isChanged |= drawSwitchRow("Translucency",
                               "Blending with what is already drawn. Off, translucent surfaces "
                               "are drawn opaque.",
                               changed.translucency);
    core::drawPropertyLabel("Texture Filter",
                            "Point shows each texel as a square, to read the texture itself.");
    if (const std::optional<TextureFilter> picked =
            drawEnumCombo("##textureFilter", TextureFilterLabels, mode.textureFilter)) {
        changed.textureFilter = *picked;
        isChanged = true;
    }
    return isChanged ? std::optional(changed) : std::nullopt;
}

[[nodiscard]] ViewportBarEdit drawShadingForm(const ViewportView& view,
                                              const std::optional<ViewportChoices>& choices) {
    ViewportBarEdit edit;
    if (!core::beginProperties("##shadingForm", LabelWidth)) {
        return edit;
    }
    if (const std::optional<RenderMode> mode = drawPipelineRows(view.renderMode)) {
        ViewportView changed = view;
        changed.renderMode = *mode;
        edit.view = changed;
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

[[nodiscard]] std::optional<ViewportView> drawShadingButtons(const ViewportView& view) {
    std::optional<ViewportView> edit;
    for (usize i = 0; i < ShadingCount; ++i) {
        const auto shading = static_cast<Shading>(i);
        if (i > 0) {
            ImGui::SameLine(0.0F, 1.0F);
        }
        ImGui::PushID(static_cast<int>(i));
        const std::string tooltip = std::format("{} (Alt+{})", getShadingLabel(shading), i + 1);
        if (drawToggleButton(ShadingIcons[i], view.renderMode.shading == shading,
                             tooltip.c_str())) {
            ViewportView changed = view;
            changed.renderMode.shading = shading;
            edit = changed;
        }
        ImGui::PopID();
    }
    return edit;
}

} // namespace

ViewportBarEdit drawViewportBar(const ViewportView& view,
                                const std::optional<ViewportChoices>& choices) {
    ViewportBarEdit edit;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    edit.view = drawShadingButtons(view);
    ImGui::SameLine();
    const std::string shading = std::format("{} Shading", core::icons::SlidersHorizontal);
    if (beginPopover(shading, ShadingPopup, false)) {
        const ViewportBarEdit form = drawShadingForm(edit.view.value_or(view), choices);
        if (form.view) {
            edit.view = form.view;
        }
        edit.choices = form.choices;
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
    for (usize i = 0; i < ShadingCount; ++i) {
        const auto key = static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i));
        if (ImGui::IsKeyChordPressed(ImGuiKeyChord{ImGuiMod_Alt} | key)) {
            ViewportView changed = view;
            changed.renderMode.shading = static_cast<Shading>(i);
            return changed;
        }
    }
    return std::nullopt;
}

} // namespace toki::viewport
