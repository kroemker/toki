#include "UnsavedEditsDialog.h"

#include "app/core/domain/UnsavedEditsQuestion.h"

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr const char* Title = "Unsaved Edits###unsavedEdits";
constexpr float WrapWidth = 28.0F;

} // namespace

void UnsavedEditsDialog::ask(std::vector<std::string> askedTitles, std::string askedDoing,
                             Answered askedAnswered) {
    titles = std::move(askedTitles);
    doing = std::move(askedDoing);
    answered = std::move(askedAnswered);
}

void UnsavedEditsDialog::answer(const UnsavedChoice choice) {
    const Answered taken = std::move(answered);
    answered = nullptr;
    ImGui::CloseCurrentPopup();
    if (taken) {
        taken(choice);
    }
}

void UnsavedEditsDialog::draw() {
    if (!isAsking()) {
        return;
    }
    if (!ImGui::IsPopupOpen(Title)) {
        ImGui::OpenPopup(Title);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal(
            Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * WrapWidth);
    ImGui::TextUnformatted(makeUnsavedEditsQuestion(titles, doing).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(titles.size() > 1 ? "Save All" : "Save") ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        answer(UnsavedChoice::Save);
    } else {
        ImGui::SameLine();
        if (ImGui::Button("Don't Save")) {
            answer(UnsavedChoice::DontSave);
        } else {
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                answer(UnsavedChoice::Cancel);
            }
        }
    }
    ImGui::EndPopup();
}

} // namespace toki::core
