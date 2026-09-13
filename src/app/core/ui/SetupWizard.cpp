#include "SetupWizard.h"

#include "app/core/Types.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

namespace toki::core {

namespace {

constexpr const char* PopupId = "###setup_wizard";
constexpr float PopupWidth = 520.0F;

void setBuffer(std::array<char, 512>& buffer, std::string_view text) {
    buffer.fill('\0');
    const usize length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
}

[[nodiscard]] bool isExistingDirectory(const std::filesystem::path& path) {
    std::error_code status;
    return std::filesystem::is_directory(path, status);
}

} // namespace

void SetupWizard::beginFirstRun() {
    step = Step::Welcome;
    active = true;
    cancellable = false;
    pathInput.fill('\0');
}

void SetupWizard::beginChangePath(const std::optional<std::filesystem::path>& current) {
    step = Step::ChoosePath;
    active = true;
    cancellable = true;
    setBuffer(pathInput, current ? toUtf8(*current) : std::string{});
}

void SetupWizard::drawWelcome() {
    ImGui::PushFont(fonts.getStrong(), Fonts::BodySize);
    ImGui::TextUnformatted("Welcome to Toki");
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0F, space::Small));
    ImGui::TextWrapped("Toki reads scenes, objects and tables straight out of a HackerOoT "
                       "source tree. Nothing is extracted from a ROM, and no build is needed "
                       "to look at an asset.");
    ImGui::Dummy(ImVec2(0.0F, space::Small));
    ImGui::TextWrapped("Point it at your local checkout to get started.");

    ImGui::Dummy(ImVec2(0.0F, space::Large));
    if (ImGui::Button("Get started")) {
        step = Step::ChoosePath;
    }
}

std::optional<std::filesystem::path> SetupWizard::drawPathStep(SDL_Window* parent) {
    if (const std::optional<std::filesystem::path> chosen = folderDialog.takeChoice()) {
        setBuffer(pathInput, toUtf8(*chosen));
    }

    ImGui::PushFont(fonts.getStrong(), Fonts::BodySize);
    ImGui::TextUnformatted("HackerOoT checkout");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0F, space::Small));

    const float browseWidth =
        ImGui::CalcTextSize("Browse\u2026").x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth -
                            ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##path", "D:\\Projects\\HackerOoT", pathInput.data(),
                             pathInput.size());

    ImGui::SameLine();
    ImGui::BeginDisabled(folderDialog.isPending());
    if (ImGui::Button("Browse\u2026")) {
        const std::filesystem::path start = fromUtf8(pathInput.data());
        folderDialog.open(parent, isExistingDirectory(start) ? start : std::filesystem::path{});
    }
    ImGui::EndDisabled();

    std::filesystem::path path = fromUtf8(pathInput.data());
    const bool empty = pathInput[0] == '\0';
    const bool valid = !empty && isExistingDirectory(path);

    ImGui::Dummy(ImVec2(0.0F, space::Small));
    if (!empty && !valid) {
        ImGui::TextColored(palette::Danger, "%s  That folder does not exist.", icons::Warning);
    } else {
        ImGui::NewLine();
    }

    ImGui::Dummy(ImVec2(0.0F, space::Medium));
    if (cancellable) {
        if (ImGui::Button("Cancel")) {
            active = false;
            ImGui::CloseCurrentPopup();
            return std::nullopt;
        }
    } else if (ImGui::Button("Back")) {
        step = Step::Welcome;
    }
    ImGui::SameLine();

    ImGui::BeginDisabled(!valid);
    const bool confirmed = ImGui::Button(cancellable ? "Use this folder" : "Finish");
    ImGui::EndDisabled();

    if (!confirmed) {
        return std::nullopt;
    }

    active = false;
    ImGui::CloseCurrentPopup();
    return path;
}

std::optional<std::filesystem::path> SetupWizard::draw(SDL_Window* parent) {
    if (!active) {
        return std::nullopt;
    }

    if (!ImGui::IsPopupOpen(PopupId)) {
        ImGui::OpenPopup(PopupId);
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(PopupWidth, 0.0F), ImGuiCond_Always);

    std::optional<std::filesystem::path> result;
    if (ImGui::BeginPopupModal(PopupId, nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        if (step == Step::Welcome) {
            drawWelcome();
        } else {
            result = drawPathStep(parent);
        }
        ImGui::EndPopup();
    }
    return result;
}

} // namespace toki::core
