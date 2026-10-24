#include "ConstantSelectable.h"

#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <format>
#include <string>
#include <string_view>

namespace toki::core {

bool drawConstantSelectable(const std::string_view label, const std::string_view name,
                            const bool isSelected) {
    const std::string id = std::format("{}##{}", label, name);
    const bool isClicked = ImGui::Selectable(id.c_str(), isSelected);
    drawConstantName(label, name);
    return isClicked;
}

void drawConstantName(const std::string_view label, const std::string_view name) {
    if (name.empty() || name == label) {
        return;
    }
    const std::string shown(name);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(shown.c_str()).x);
    ImGui::TextColored(palette::TextMuted, "%s", shown.c_str());
}

bool isConstantFilteredIn(const std::string_view label, const std::string_view name,
                          const std::string_view filter) {
    return containsIgnoringCase(label, filter) || containsIgnoringCase(name, filter);
}

} // namespace toki::core
