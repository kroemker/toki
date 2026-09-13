#include "Theme.h"

namespace toki::core {

namespace {

[[nodiscard]] constexpr ImVec4 fade(const ImVec4& colour, float alpha) {
    return ImVec4{colour.x, colour.y, colour.z, colour.w * alpha};
}

void applyMetrics(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(space::Large, space::Large);
    style.FramePadding = ImVec2(space::Medium, space::Small + 1.0F);
    style.CellPadding = ImVec2(space::Medium, space::Small);
    style.ItemSpacing = ImVec2(space::Medium, space::Small + 2.0F);
    style.ItemInnerSpacing = ImVec2(space::Small + 2.0F, space::Small);
    style.IndentSpacing = space::Huge;
    style.ScrollbarSize = space::Large;
    style.GrabMinSize = space::Large;

    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBarBorderSize = 1.0F;

    style.WindowRounding = 0.0F;
    style.ChildRounding = space::Small;
    style.FrameRounding = space::Small;
    style.PopupRounding = space::Small;
    style.ScrollbarRounding = space::Small;
    style.GrabRounding = 3.0F;
    style.TabRounding = space::Small;

    style.WindowTitleAlign = ImVec2(0.0F, 0.5F);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1.0F;
    style.DockingSeparatorSize = 2.0F;
}

void applyColors(ImGuiStyle& style) {
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = palette::Text;
    colors[ImGuiCol_TextDisabled] = palette::TextDisabled;
    colors[ImGuiCol_WindowBg] = palette::Surface;
    colors[ImGuiCol_ChildBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_PopupBg] = palette::SurfaceRaised;
    colors[ImGuiCol_Border] = palette::Border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);

    colors[ImGuiCol_FrameBg] = palette::Background;
    colors[ImGuiCol_FrameBgHovered] = palette::SurfaceRaised;
    colors[ImGuiCol_FrameBgActive] = palette::SurfaceHovered;

    colors[ImGuiCol_TitleBg] = palette::Background;
    colors[ImGuiCol_TitleBgActive] = palette::Background;
    colors[ImGuiCol_TitleBgCollapsed] = palette::Background;
    colors[ImGuiCol_MenuBarBg] = palette::Background;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_ScrollbarGrab] = palette::SurfaceActive;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette::TextDisabled;
    colors[ImGuiCol_ScrollbarGrabActive] = palette::TextMuted;

    colors[ImGuiCol_CheckMark] = palette::Accent;
    colors[ImGuiCol_SliderGrab] = palette::Accent;
    colors[ImGuiCol_SliderGrabActive] = palette::AccentHovered;

    colors[ImGuiCol_Button] = palette::SurfaceRaised;
    colors[ImGuiCol_ButtonHovered] = palette::SurfaceHovered;
    colors[ImGuiCol_ButtonActive] = palette::SurfaceActive;

    colors[ImGuiCol_Header] = palette::AccentMuted;
    colors[ImGuiCol_HeaderHovered] = fade(palette::Accent, 0.45F);
    colors[ImGuiCol_HeaderActive] = fade(palette::Accent, 0.60F);

    colors[ImGuiCol_Separator] = palette::Border;
    colors[ImGuiCol_SeparatorHovered] = palette::Accent;
    colors[ImGuiCol_SeparatorActive] = palette::AccentHovered;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_ResizeGripHovered] = palette::AccentMuted;
    colors[ImGuiCol_ResizeGripActive] = palette::Accent;

    colors[ImGuiCol_Tab] = palette::Background;
    colors[ImGuiCol_TabHovered] = palette::SurfaceHovered;
    colors[ImGuiCol_TabSelected] = palette::Surface;
    colors[ImGuiCol_TabSelectedOverline] = palette::Accent;
    colors[ImGuiCol_TabDimmed] = palette::Background;
    colors[ImGuiCol_TabDimmedSelected] = palette::Surface;
    colors[ImGuiCol_TabDimmedSelectedOverline] = fade(palette::Accent, 0.40F);

    colors[ImGuiCol_DockingPreview] = palette::AccentMuted;
    colors[ImGuiCol_DockingEmptyBg] = palette::Background;

    colors[ImGuiCol_PlotLines] = palette::TextMuted;
    colors[ImGuiCol_PlotLinesHovered] = palette::Accent;
    colors[ImGuiCol_PlotHistogram] = palette::Accent;
    colors[ImGuiCol_PlotHistogramHovered] = palette::AccentHovered;

    colors[ImGuiCol_TableHeaderBg] = palette::SurfaceRaised;
    colors[ImGuiCol_TableBorderStrong] = palette::Border;
    colors[ImGuiCol_TableBorderLight] = fade(palette::Border, 0.50F);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    colors[ImGuiCol_TableRowBgAlt] = fade(palette::SurfaceRaised, 0.35F);

    colors[ImGuiCol_TextSelectedBg] = palette::AccentMuted;
    colors[ImGuiCol_DragDropTarget] = palette::Accent;
    colors[ImGuiCol_NavCursor] = palette::Accent;
    colors[ImGuiCol_NavWindowingHighlight] = palette::Accent;
    colors[ImGuiCol_NavWindowingDimBg] = fade(palette::Background, 0.60F);
    colors[ImGuiCol_ModalWindowDimBg] = fade(palette::Background, 0.70F);
}

} // namespace

void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    applyMetrics(style);
    applyColors(style);
}

} // namespace toki::core
