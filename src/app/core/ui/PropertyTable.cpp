#include "PropertyTable.h"

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

} // namespace toki::core
