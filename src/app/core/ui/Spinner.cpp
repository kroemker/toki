#include "Spinner.h"

#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <numbers>
#include <string_view>

namespace toki::core {

namespace {

constexpr f32 TurnsPerSecond = 1.2F;
constexpr f32 ArcShare = 0.7F;
constexpr f32 ThicknessShare = 0.14F;
constexpr int ArcSegments = 24;
constexpr f32 CenteredSpinnerEms = 2.0F;
constexpr ImVec4 CoverColor{palette::Surface.x, palette::Surface.y, palette::Surface.z, 0.7F};

void drawSpinnerAt(const ImVec2 center, const f32 size) {
    constexpr f32 Turn = 2.0F * std::numbers::pi_v<f32>;
    const f32 thickness = std::max(1.5F, size * ThicknessShare);
    const f32 radius = (size - thickness) / 2.0F;
    const f32 start = static_cast<f32>(ImGui::GetTime()) * TurnsPerSecond * Turn;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircle(center, radius, ImGui::GetColorU32(palette::AccentMuted), 0, thickness);
    drawList->PathArcTo(center, radius, start, start + (Turn * ArcShare), ArcSegments);
    drawList->PathStroke(ImGui::GetColorU32(palette::Accent), ImDrawFlags_None, thickness);
}

void drawMutedText(const std::string_view label) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette::TextMuted);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
}

} // namespace

void drawSpinner(const f32 size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    if (ImGui::IsItemVisible()) {
        drawSpinnerAt(ImVec2(origin.x + (size / 2.0F), origin.y + (size / 2.0F)), size);
    }
}

void drawCenteredSpinner(const std::string_view label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 size = ImGui::GetFontSize() * CenteredSpinnerEms;
    const ImVec2 text = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 cursor = ImGui::GetCursorPos();
    const f32 height = size + style.ItemSpacing.y + text.y;
    const f32 top = cursor.y + std::max(0.0F, (available.y - height) / 2.0F);
    ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0F, (available.x - size) / 2.0F), top));
    drawSpinner(size);
    ImGui::SetCursorPosX(cursor.x + std::max(0.0F, (available.x - text.x) / 2.0F));
    drawMutedText(label);
}

void drawBusyCover(const ImVec2 start, const ImVec2 size, const std::string_view label) {
    ImGui::SetCursorPos(start);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 covered(size.x > 0.0F ? size.x : available.x,
                         size.y > 0.0F ? size.y : available.y);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, CoverColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    if (ImGui::BeginChild("##busyCover", covered, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoSavedSettings)) {
        drawCenteredSpinner(label);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace toki::core
