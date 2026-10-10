#include "NewActorsPrompt.h"

#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr const char* PromptName = "New actors";
constexpr float ListHeight = 12.0F;

} // namespace

void NewActorsPrompt::offer(std::vector<std::string> actors,
                            const std::vector<std::string>& dismissed) {
    if (actors.empty() || actors == dismissed || actors == offered) {
        return;
    }
    offered = std::move(actors);
    isOpening = true;
}

std::optional<NewActorsAnswer> NewActorsPrompt::draw() {
    if (isOpening) {
        ImGui::OpenPopup(PromptName);
        isOpening = false;
    }
    if (!ImGui::BeginPopupModal(PromptName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return std::nullopt;
    }
    std::optional<NewActorsAnswer> answer;
    ImGui::TextUnformatted(
        std::format("{} new actors were found. Derive them?", offered.size()).c_str());
    ImGui::TextColored(core::palette::TextMuted,
                       "Deriving reads what Toki knows of each into the actor database, and "
                       "takes a moment for each actor read from its C.");
    if (ImGui::BeginChild("##actors",
                          ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing() * ListHeight),
                          ImGuiChildFlags_Borders)) {
        for (const std::string& actor : offered) {
            ImGui::TextUnformatted(actor.c_str());
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Derive them")) {
        answer = NewActorsAnswer{.isDerived = true, .actors = offered};
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now")) {
        answer = NewActorsAnswer{.isDerived = false, .actors = offered};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return answer;
}

} // namespace toki::actor
