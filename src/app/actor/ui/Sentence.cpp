#include "Sentence.h"

#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace toki::actor {

void Sentence::placeNext(const float width) {
    if (!isFirst) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < width) {
            ImGui::NewLine();
        }
    }
    isFirst = false;
}

void Sentence::addWords(const std::string_view words) {
    placeNext(ImGui::CalcTextSize(words.data(), words.data() + words.size()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(core::palette::TextMuted, "%.*s", static_cast<int>(words.size()),
                       words.data());
}

bool Sentence::drawToken(const char* id, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Accent);
    ImGui::PushStyleColor(ImGuiCol_Button, core::palette::SurfaceRaised);
    ImGui::PushID(id);
    const bool isClicked = ImGui::Button(text.c_str());
    ImGui::PopID();
    ImGui::PopStyleColor(2);
    if (isClicked) {
        ImGui::OpenPopup(id);
    }
    return ImGui::IsPopupOpen(id);
}

bool Sentence::addButton(const char* label, const char* tooltip) {
    placeNext(ImGui::CalcTextSize(label).x + (ImGui::GetStyle().FramePadding.x * 2.0F));
    const bool isClicked = ImGui::SmallButton(label);
    ImGui::SetItemTooltip("%s", tooltip);
    return isClicked;
}

} // namespace toki::actor
