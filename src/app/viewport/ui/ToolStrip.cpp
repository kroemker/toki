#include "ToolStrip.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace toki::viewport {

namespace {

struct ToolButton {
    const char* icon;
    const char* name;
    core::Action action;
    bool isBuilt;
    /// What the tooltip names the key of while `action` has none: the key
    /// that does with the mouse what the tool does with its gizmo.
    std::optional<core::Action> keyed = std::nullopt;
};

struct ModeButton {
    ToolButton button;
    SelectionMode mode;
};

constexpr std::array<ModeButton, 4> ModeButtons{{
    {{core::icons::DotOutline, "Vertex", core::Action::VertexMode, true}, SelectionMode::Vertex},
    {{core::icons::LineSegment, "Edge", core::Action::EdgeMode, true}, SelectionMode::Edge},
    {{core::icons::Polygon, "Face", core::Action::FaceMode, true}, SelectionMode::Face},
    {{core::icons::Cube, "Node", core::Action::NodeMode, true}, SelectionMode::Node},
}};

constexpr std::array<ToolButton, 4> ToolButtons{{
    {core::icons::Cursor, "Select", core::Action::SelectTool, true},
    {core::icons::ArrowsOutCardinal, "Move", core::Action::MoveTool, true, core::Action::Grab},
    {core::icons::ArrowClockwise, "Rotate", core::Action::RotateTool, true, core::Action::Turn},
    {core::icons::Resize, "Scale", core::Action::ScaleTool, true, core::Action::Stretch},
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

/// An icon button drawn as pressed while `isActive`, and disabled unless
/// `canUse`. True when it was clicked.
bool drawIconButton(const ToolButton& button, const bool isActive, const ImVec2 size,
                    const bool canUse = true) {
    const bool isUsable = button.isBuilt && canUse;
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
        const core::Action keyed = button.keyed && core::getShortcutLabel(button.action).empty()
                                       ? *button.keyed
                                       : button.action;
        const std::string named = core::makeShortcutText(button.name, keyed);
        ImGui::SetTooltip("%s", button.isBuilt ? named.c_str()
                                               : std::format("{}, not built yet", named).c_str());
    }
    return isClicked && isUsable;
}

/// Nodes are never scaled: an actor sets its own scale.
[[nodiscard]] bool isToolOffered(const ViewportTools& tools, const EditTool tool) {
    return tool != EditTool::Scale || tools.mode != SelectionMode::Node;
}

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
        const SelectionMode mode = ModeButtons[i].mode;
        if (drawIconButton(ModeButtons[i].button, tools.mode == mode, ImVec2(0.0F, 0.0F),
                           mode == SelectionMode::Node || canEdit)) {
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
        ImGui::SetTooltip("%s", named.c_str());
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
            const auto tool = static_cast<EditTool>(i);
            if (!isToolOffered(tools, tool)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
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
    for (const ModeButton& button : ModeButtons) {
        if (button.button.isBuilt && (canEdit || button.mode == SelectionMode::Node) &&
            core::isActionPressed(button.button.action)) {
            setSelectionMode(tools, button.mode);
        }
    }
    for (usize i = 0; i < ToolButtons.size(); ++i) {
        const auto tool = static_cast<EditTool>(i);
        if (ToolButtons[i].isBuilt && isToolOffered(tools, tool) &&
            core::isActionPressed(ToolButtons[i].action)) {
            tools.tool = tool;
        }
    }
    if (canEdit && core::isActionPressed(core::Action::ToggleEditMode)) {
        tools.mode = tools.mode == SelectionMode::Node ? tools.lastEditMode : SelectionMode::Node;
    }
    if (!isToolOffered(tools, tools.tool)) {
        tools.tool = EditTool::Select;
    }
}

} // namespace toki::viewport
