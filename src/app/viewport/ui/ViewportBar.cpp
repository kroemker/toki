#include "ViewportBar.h"

#include "app/core/ui/Hotkeys.h"
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
    core::icons::ImageSquare, core::icons::Sphere,   core::icons::Checkerboard,
    core::icons::Compass,     core::icons::Gradient, core::icons::CubeTransparent};

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
    if (choices && choices->hasCollision) {
        ViewportChoices changed = edit.choices.value_or(*choices);
        if (drawSwitchRow("Collision",
                          "The layer's collision, drawn see-through over the rooms shown, or "
                          "opaque with every room hidden. While it is shown, its polygons no "
                          "face has and the solid faces of hidden rooms can be picked.",
                          changed.showsCollision)) {
            edit.choices = changed;
        }
        if (changed.showsCollision && !changed.collisionColorings.empty()) {
            core::drawPropertyLabel("Collision Color",
                                    "What the shown collision is coloured by: Kind colours "
                                    "floors, walls and ceilings apart; a setting colours each of "
                                    "its values, and leaves grey what is without it.");
            if (const std::optional<usize> chosen = drawNameCombo(
                    "##collisionColoring", changed.collisionColorings, changed.collisionColoring)) {
                changed.collisionColoring = *chosen;
                edit.choices = std::move(changed);
            }
        }
    }
    ViewportView changed = view;
    bool isChanged = drawSwitchRow("Flag Links",
                                   "A line from each actor that sets a flag to each that reads "
                                   "it; hover a flag to see only its own.",
                                   changed.overlays.flagLinks);
    isChanged |= drawSwitchRow("Grid",
                               "A floor grid on the XZ plane around the camera's target, "
                               "a line every 100 units.",
                               changed.overlays.grid);
    isChanged |= drawSwitchRow(
        "X-Ray",
        core::makeShortcutText("What walls hide of the overlays, drawn faintly through them",
                               core::Action::ToggleXray)
            .c_str(),
        changed.overlays.xray);
    isChanged |= drawSwitchRow("Selection Highlight",
                               "What is selected, tinted and outlined where the frame drew it.",
                               changed.overlays.selectionHighlight);
    isChanged |=
        drawSwitchRow("Selection Box", "A box around what the frame drew of each thing selected.",
                      changed.overlays.selectionBox);
    if (isChanged) {
        edit.view = changed;
    }
    core::endProperties();
    return edit;
}

[[nodiscard]] usize countShownOverlays(const ViewportView& view,
                                       const std::optional<ViewportChoices>& choices) {
    usize count = (view.overlays.flagLinks ? 1 : 0) + (view.overlays.grid ? 1 : 0);
    if (choices && choices->actors && *choices->actors != ActorDisplay::Hidden) {
        ++count;
    }
    if (choices && choices->showsCollision) {
        ++count;
    }
    if (choices && choices->showsWaterBoxes) {
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
        const std::string tooltip = core::makeShortcutText(
            getShadingLabel(shading),
            static_cast<core::Action>(static_cast<usize>(core::Action::ShadingN64) + i));
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

    const ViewportView& current = edit.view ? *edit.view : view;
    const std::string xray = std::format("{}###xray", core::icons::Intersect);
    const std::string toggle = current.overlays.isShown ? core::icons::Eye : core::icons::EyeSlash;
    const std::string overlays =
        std::format("Overlays {}###overlaysButton", countShownOverlays(current, choices));
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = findButtonWidth(xray) + spacing + findButtonWidth(toggle) + spacing +
                        findButtonWidth(overlays);
    ImGui::SetCursorScreenPos(ImVec2(start.x + available - width, start.y));
    if (drawToggleButton(xray, current.overlays.xray,
                         core::makeShortcutText(current.overlays.xray
                                                    ? "Hide what walls hide"
                                                    : "Show overlays through walls",
                                                core::Action::ToggleXray)
                             .c_str())) {
        ViewportView changed = current;
        changed.overlays.xray = !changed.overlays.xray;
        edit.view = changed;
    }
    ImGui::SameLine();
    if (drawToggleButton(toggle, !current.overlays.isShown,
                         core::makeShortcutText(current.overlays.isShown ? "Hide all overlays"
                                                                         : "Show all overlays",
                                                core::Action::ToggleOverlays)
                             .c_str())) {
        ViewportView changed = edit.view.value_or(view);
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

bool beginBarMenu(const std::string& label, const char* popup) {
    return beginPopover(label, popup, false);
}

std::optional<ViewportView> applyViewportHotkeys(const ViewportView& view) {
    if (core::isActionPressed(core::Action::ToggleOverlays)) {
        ViewportView changed = view;
        changed.overlays.isShown = !changed.overlays.isShown;
        return changed;
    }
    if (core::isActionPressed(core::Action::ToggleXray)) {
        ViewportView changed = view;
        changed.overlays.xray = !changed.overlays.xray;
        return changed;
    }
    for (usize i = 0; i < ShadingCount; ++i) {
        if (core::isActionPressed(
                static_cast<core::Action>(static_cast<usize>(core::Action::ShadingN64) + i))) {
            ViewportView changed = view;
            changed.renderMode.shading = static_cast<Shading>(i);
            return changed;
        }
    }
    return std::nullopt;
}

} // namespace toki::viewport
