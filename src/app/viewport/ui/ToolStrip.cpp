#include "ToolStrip.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <array>
#include <format>
#include <span>
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
    {core::icons::Polygon, "Face", core::Action::FaceMode, true},
    {core::icons::LineSegment, "Edge", core::Action::EdgeMode, true},
    {core::icons::DotOutline, "Vertex", core::Action::VertexMode, true},
}};

constexpr std::array<ToolButton, 4> ToolButtons{{
    {core::icons::Cursor, "Select: click, or drag a box", core::Action::SelectTool, true},
    {core::icons::ArrowsOutCardinal, "Move", core::Action::MoveTool, true},
    {core::icons::ArrowClockwise, "Rotate", core::Action::RotateTool, true},
    {core::icons::Resize, "Scale, for geometry; actors set their own scale",
     core::Action::ScaleTool, true},
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

/// An icon button drawn as pressed while `isActive`, disabled and saying why
/// when `whyNot` says it cannot be used. True when it was clicked.
bool drawIconButton(const ToolButton& button, const bool isActive, const ImVec2 size,
                    const char* whyNot = nullptr) {
    const bool isUsable = button.isBuilt && whyNot == nullptr;
    ImGui::BeginDisabled(!isUsable);
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
        std::string tooltip = named;
        if (!button.isBuilt) {
            tooltip = std::format("{}, not built yet", named);
        } else if (whyNot != nullptr) {
            tooltip = std::format("{}. {}", named, whyNot);
        }
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    return isClicked && isUsable;
}

constexpr const char* NothingToEdit = "What is shown has no faces, edges or vertices to edit.";

} // namespace

void setSelectionMode(ViewportTools& tools, const SelectionMode mode) {
    tools.mode = mode;
    if (mode != SelectionMode::Node) {
        tools.lastEditMode = mode;
    }
}

bool drawSelectionModes(ViewportTools& tools, const bool canEdit) {
    bool isPicked = false;
    for (usize i = 0; i < ModeButtons.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0F, 1.0F);
        }
        ImGui::PushID(static_cast<int>(i));
        const auto mode = static_cast<SelectionMode>(i);
        const char* whyNot = mode != SelectionMode::Node && !canEdit ? NothingToEdit : nullptr;
        if (drawIconButton(ModeButtons[i], tools.mode == mode, ImVec2(0.0F, 0.0F), whyNot)) {
            setSelectionMode(tools, mode);
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

void drawStripAction(const StripAction& action, const ImVec2 size) {
    ImGui::BeginDisabled(!action.whyNot.empty());
    const bool isClicked = ImGui::Button(action.icon, size);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) {
        const std::string named =
            action.action ? core::makeShortcutText(action.name, *action.action) : action.name;
        const std::string tooltip =
            action.whyNot.empty() ? named : std::format("{}. {}", named, action.whyNot);
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    if (isClicked && action.whyNot.empty() && action.run) {
        action.run();
    }
}

void drawToolStrip(ViewportTools& tools, const ImVec2 position,
                   const std::span<const StripAction> actions) {
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
        if (tools.mode != SelectionMode::Node && !actions.empty()) {
            ImGui::Separator();
            for (usize i = 0; i < actions.size(); ++i) {
                ImGui::PushID(static_cast<int>(ToolButtons.size() + i));
                drawStripAction(actions[i], ImVec2(side, side));
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void applyToolHotkeys(ViewportTools& tools, const bool canEdit) {
    for (usize i = 0; i < ModeButtons.size(); ++i) {
        const auto mode = static_cast<SelectionMode>(i);
        if (ModeButtons[i].isBuilt && (canEdit || mode == SelectionMode::Node) &&
            core::isActionPressed(ModeButtons[i].action)) {
            setSelectionMode(tools, mode);
        }
        if (ToolButtons[i].isBuilt && core::isActionPressed(ToolButtons[i].action)) {
            tools.tool = static_cast<EditTool>(i);
        }
    }
    if (canEdit && core::isActionPressed(core::Action::ToggleEditMode)) {
        tools.mode = tools.mode == SelectionMode::Node ? tools.lastEditMode : SelectionMode::Node;
    }
}

} // namespace toki::viewport
