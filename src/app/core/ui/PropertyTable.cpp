#include "PropertyTable.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string_view>

namespace toki::core {

namespace {

constexpr float MinimumTableWidth = 26.0F;
constexpr float HelpWidth = 28.0F;

} // namespace

bool beginProperties(const char* id, const float labelWidth) {
    const float width =
        std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * MinimumTableWidth);
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp, ImVec2(width, 0.0F))) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * labelWidth);
    ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void endProperties() {
    ImGui::EndTable();
}

void drawPropertyLabel(const std::string_view label, const std::string_view help) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    if (!help.empty() && ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * HelpWidth);
        ImGui::TextUnformatted(help.data(), help.data() + help.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void drawPropertyLabel(const std::string_view label, const std::string_view help,
                       const std::string_view warning) {
    drawPropertyLabel(label, help);
    if (warning.empty()) {
        return;
    }
    const float iconWidth = ImGui::CalcTextSize(icons::Warning).x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(start.x + ImGui::GetContentRegionAvail().x - iconWidth, start.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette::Warning, "%s", icons::Warning);
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(warning.data(), warning.data() + warning.size());
        ImGui::EndTooltip();
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::SetNextItemWidth(-(iconWidth + spacing));
}

} // namespace toki::core
