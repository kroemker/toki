#include "SaveSceneDialog.h"

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
                                 std::function<void(const std::filesystem::path&)> saved)
    : core::Dialog("saveScene", "Save Scene", core::icons::FloppyDisk, core::DialogMode::Modal,
                   Width, Height),
      workspaceService(workspaceService), saved(std::move(saved)) {}

void SaveSceneDialog::open(const Scene& opened, const SceneModel* edited) {
    scene = Scene{.name = opened.name,
                  .directory = opened.directory,
                  .images = opened.images,
                  .files = opened.files};
    sceneModel = edited != nullptr ? std::optional(*edited) : std::nullopt;
    show();
    requestPlan();
}

void SaveSceneDialog::requestPlan() {
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
        });
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
            requestPlan();
        }
        ImGui::EndDisabled();
    }
    if (editFlow.draw(workspace->getRoot()) && saved) {
        saved(findSavedDirectory(workspace->getRoot(), scene->directory));
    }
}

} // namespace toki::scene
