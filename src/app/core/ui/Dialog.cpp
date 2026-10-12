#include "Dialog.h"

#include <imgui.h>

#include <format>
#include <string>
#include <utility>

namespace toki::core {

Dialog::Dialog(std::string id, std::string title, const char* icon, const DialogMode mode,
               const f32 width, const f32 height)
    : id(std::move(id)), menuLabel(std::format("{}  {}", icon, title)),
      windowTitle(std::format("{}  {}###dialog.{}", icon, title, this->id)), mode(mode),
      width(width), height(height) {}

void Dialog::drawWindow() {
    if (!shown) {
        return;
    }
    const f32 em = ImGui::GetFontSize();
    const ImGuiCond placement =
        mode == DialogMode::Modal ? ImGuiCond_Appearing : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), placement, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(em * width, em * height), placement);
    if (mode == DialogMode::Modal) {
        if (!ImGui::IsPopupOpen(windowTitle.c_str())) {
            ImGui::OpenPopup(windowTitle.c_str());
        }
        if (ImGui::BeginPopupModal(windowTitle.c_str(), &shown, ImGuiWindowFlags_NoSavedSettings)) {
            draw();
            ImGui::EndPopup();
        }
        return;
    }
    if (ImGui::Begin(windowTitle.c_str(), &shown, ImGuiWindowFlags_NoDocking)) {
        draw();
    }
    ImGui::End();
}

} // namespace toki::core
