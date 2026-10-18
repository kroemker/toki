#include "ToolStrip.h"

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
    const char* key;
    bool isBuilt;
};

constexpr std::array<ToolButton, 4> ModeButtons{{
    {core::icons::Cube, "Object", "1", true},
    {core::icons::Polygon, "Face", "2", false},
    {core::icons::LineSegment, "Edge", "3", false},
    {core::icons::DotOutline, "Vertex", "4", false},
}};

constexpr std::array<ToolButton, 4> ToolButtons{{
    {core::icons::Cursor, "Select", "Q", true},
    {core::icons::ArrowsOutCardinal, "Move", "W", true},
    {core::icons::ArrowClockwise, "Rotate", "E", false},
    {core::icons::Resize, "Scale", "R", false},
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
        const std::string tooltip =
            button.isBuilt ? std::format("{} ({})", button.name, button.key)
                           : std::format("{} ({}), not built yet", button.name, button.key);
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
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt) {
        return;
    }
    constexpr std::array<ImGuiKey, 4> ModeKeys{ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4};
    constexpr std::array<ImGuiKey, 4> ToolKeys{ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R};
    for (usize i = 0; i < ModeKeys.size(); ++i) {
        if (ModeButtons[i].isBuilt && ImGui::IsKeyPressed(ModeKeys[i], false)) {
            tools.mode = static_cast<SelectionMode>(i);
        }
        if (ToolButtons[i].isBuilt && ImGui::IsKeyPressed(ToolKeys[i], false)) {
            tools.tool = static_cast<EditTool>(i);
        }
    }
}

} // namespace toki::viewport
