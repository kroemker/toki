#include "SaveSceneDialog.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneSave.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/EditPlan.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace toki::scene {

namespace {

constexpr float Width = 44.0F;
constexpr float Height = 30.0F;

} // namespace

SaveSceneDialog::SaveSceneDialog(const workspace::WorkspaceService& workspaceService,
                                 std::function<void(const std::filesystem::path&)> saved,
                                 std::function<void()> stopReviewing)
    : core::Dialog("saveScene", "Save Scene", core::icons::FloppyDisk, core::DialogMode::Modal,
                   Width, Height),
      workspaceService(workspaceService), saved(std::move(saved)),
      stopReviewing(std::move(stopReviewing)) {}

void SaveSceneDialog::open(const Scene& opened, const SceneModel* edited, const bool isReviewed) {
    scene = Scene{.name = opened.name,
                  .directory = opened.directory,
                  .images = opened.images,
                  .files = opened.files};
    sceneModel = edited != nullptr ? std::optional(*edited) : std::nullopt;
    isDoNotShowAgain = false;
    isSavingUnshown = !isReviewed;
    if (isReviewed) {
        show();
    }
    requestPlan(isReviewed);
}

void SaveSceneDialog::update() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (!isSavingUnshown || isShown() || workspace == nullptr || !scene) {
        return;
    }
    if (editFlow.update(workspace->getRoot())) {
        isSavingUnshown = false;
        core::logOk("saved {}: {}", scene->name, editFlow.getStatus());
        finishSave(workspace->getRoot());
    } else if (editFlow.hasFailed()) {
        isSavingUnshown = false;
        show();
    }
}

void SaveSceneDialog::finishSave(const std::filesystem::path& root) {
    if (saved) {
        saved(findSavedDirectory(root, scene->directory));
    }
}

void SaveSceneDialog::requestPlan(const bool isReviewed) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr || !scene) {
        return;
    }
    editFlow.requestPlan(
        workspace->getRoot(),
        [shared = workspaceService.getSharedWorkspace(), opened = *scene,
         edited = sceneModel]() -> std::expected<hackeroot::EditPlan, std::string> {
            if (shared == nullptr) {
                return std::unexpected(std::string("no checkout is open"));
            }
            return planSceneSave(shared,
                                 SceneSaveInput{.name = opened.name,
                                                .directory = opened.directory,
                                                .files = opened.files,
                                                .model = edited ? *edited : SceneModel(opened)});
        },
        isReviewed);
}

void SaveSceneDialog::draw() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr || !scene) {
        ImGui::TextColored(core::palette::TextMuted, "No scene is open.");
        return;
    }
    ImGui::TextUnformatted(scene->name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(
        core::palette::TextMuted, "%s",
        scene->directory.lexically_relative(workspace->getRoot()).generic_string().c_str());
    ImGui::Spacing();
    if (!editFlow.isReviewing()) {
        ImGui::BeginDisabled(editFlow.isBusy());
        if (ImGui::Button("Review changes")) {
            requestPlan(true);
        }
        ImGui::EndDisabled();
    }
    if (editFlow.isReviewing()) {
        if (ImGui::Checkbox("Do Not Show Again", &isDoNotShowAgain) && isDoNotShowAgain &&
            stopReviewing) {
            stopReviewing();
        }
        ImGui::SetItemTooltip("Saves are written at once from now on, and what they warn of goes "
                              "to the log. Settings > Scenes > Review Saves shows them again.");
    }
    if (editFlow.draw(workspace->getRoot())) {
        finishSave(workspace->getRoot());
    }
}

} // namespace toki::scene
