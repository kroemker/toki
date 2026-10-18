#include "ToolStrip.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <array>
#include <format>
#include <string>

namespace toki::viewport {

namespace {

struct ToolButton {
    const char* icon;
    const char* name;
    core::Action action;
    bool isBuilt;
};

constexpr std::array<ToolButton, 4> ModeButtons{{
    {core::icons::Cube, "Node", core::Action::NodeMode, true},
    {core::icons::Polygon, "Face", core::Action::FaceMode, false},
    {core::icons::LineSegment, "Edge", core::Action::EdgeMode, false},
    {core::icons::DotOutline, "Vertex", core::Action::VertexMode, false},
}};

constexpr std::array<ToolButton, 4> ToolButtons{{
    {core::icons::Cursor, "Select: click, or drag a box", core::Action::SelectTool, true},
    {core::icons::ArrowsOutCardinal, "Move", core::Action::MoveTool, true},
    {core::icons::ArrowClockwise, "Rotate", core::Action::RotateTool, true},
    {core::icons::Resize, "Scale, for geometry; actors set their own scale",
     core::Action::ScaleTool, false},
}};

struct PickModeButton {
    const char* icon;
    PickMode mode;
    const char* tooltip;
};

constexpr std::array<PickModeButton, 3> PickModeButtons{{
    {core::icons::Selection, PickMode::Replace,
     "Pick anew: a click or a box selects only what it hits"},
    {core::icons::SelectionPlus, PickMode::Add, "Add to the selection (hold Shift)"},
    {core::icons::SelectionSlash, PickMode::Remove, "Take from the selection (hold Ctrl)"},
}};

constexpr float StripPadding = 3.0F;
constexpr float StripAlpha = 0.92F;
constexpr float StripButtonSize = 1.2F;

/// An icon button drawn as pressed while `isActive`, disabled and saying so
/// when what it picks is not built. True when it was clicked.
bool drawIconButton(const ToolButton& button, const bool isActive, const ImVec2 size) {
    ImGui::BeginDisabled(!button.isBuilt);
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, core::palette::AccentMuted);
    }
    const bool isClicked = ImGui::Button(button.icon, size);
    if (isActive) {
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
        const std::string named = core::makeShortcutText(button.name, button.action);
        const std::string tooltip =
            button.isBuilt ? named : std::format("{}, not built yet", named);
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    return isClicked && button.isBuilt;
}

} // namespace

bool drawSelectionModes(ViewportTools& tools) {
    bool isPicked = false;
    for (usize i = 0; i < ModeButtons.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0F, 1.0F);
        }
        ImGui::PushID(static_cast<int>(i));
        const auto mode = static_cast<SelectionMode>(i);
        if (drawIconButton(ModeButtons[i], tools.mode == mode, ImVec2(0.0F, 0.0F))) {
            tools.mode = mode;
            isPicked = true;
        }
        ImGui::PopID();
    }
    return isPicked;
}

void drawPickModes(ViewportTools& tools, const PickMode shown) {
    for (usize i = 0; i < PickModeButtons.size(); ++i) {
        const PickModeButton& button = PickModeButtons[i];
        if (i > 0) {
            ImGui::SameLine(0.0F, 1.0F);
        }
        ImGui::PushID(static_cast<int>(i));
        const bool isShown = shown == button.mode;
        if (isShown) {
            ImGui::PushStyleColor(ImGuiCol_Button, core::palette::AccentMuted);
        }
        if (ImGui::Button(button.icon)) {
            tools.pickMode = button.mode;
        }
        if (isShown) {
            ImGui::PopStyleColor();
        }
        ImGui::SetItemTooltip("%s", button.tooltip);
        ImGui::PopID();
    }
}

std::optional<PickMode> findHeldPickMode() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        return PickMode::Remove;
    }
    if (io.KeyShift) {
        return PickMode::Add;
    }
    return std::nullopt;
}

void drawToolStrip(ViewportTools& tools, const ImVec2 position) {
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(StripPadding, StripPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(StripPadding, StripPadding));
    ImVec4 background = core::palette::Surface;
    background.w = StripAlpha;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    if (ImGui::BeginChild("##tools", ImVec2(0.0F, 0.0F),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX |
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        const float side = ImGui::GetFrameHeight() * StripButtonSize;
        for (usize i = 0; i < ToolButtons.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto tool = static_cast<EditTool>(i);
            if (drawIconButton(ToolButtons[i], tools.tool == tool, ImVec2(side, side))) {
                tools.tool = tool;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void applyToolHotkeys(ViewportTools& tools) {
    for (usize i = 0; i < ModeButtons.size(); ++i) {
        if (ModeButtons[i].isBuilt && core::isActionPressed(ModeButtons[i].action)) {
            tools.mode = static_cast<SelectionMode>(i);
        }
        if (ToolButtons[i].isBuilt && core::isActionPressed(ToolButtons[i].action)) {
            tools.tool = static_cast<EditTool>(i);
        }
    }
}

} // namespace toki::viewport
