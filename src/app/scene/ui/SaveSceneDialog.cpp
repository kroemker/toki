#include "SaveSceneDialog.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/GeometryChanges.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneSave.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/EditPlan.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace toki::scene {

namespace {

constexpr float Width = 44.0F;
constexpr float Height = 30.0F;
constexpr float TableHeight = 10.0F;
constexpr float ChangeWidth = 8.0F;
constexpr float NumberWidth = 5.0F;

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
    geometryChanges = edited != nullptr ? listGeometryChanges(opened, *edited) : GeometryChanges{};
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
    if (editFlow.isReviewing()) {
        drawGeometryChanges();
    }
    if (editFlow.draw(workspace->getRoot())) {
        finishSave(workspace->getRoot());
    }
}

void SaveSceneDialog::drawGeometryChanges() const {
    if (geometryChanges.changes.empty()) {
        return;
    }
    const std::string header =
        std::format("Geometry · {} changed, {} lists and arrays unchanged",
                    geometryChanges.changes.size(), geometryChanges.unchanged);
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       "Every list rebuilt was run on the RSP and read back as the same faces "
                       "before it was made. Lists not named are written byte for byte as read.");
    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const f32 font = ImGui::GetFontSize();
    if (!ImGui::BeginTable("##geometry", 4, Flags, ImVec2(0.0F, font * TableHeight))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, font * ChangeWidth);
    ImGui::TableSetupColumn("Commands", ImGuiTableColumnFlags_WidthFixed, font * NumberWidth);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, font * NumberWidth);
    ImGui::TableHeadersRow();
    for (const GeometryChange& change : geometryChanges.changes) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(change.symbol.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(getGeometryChangeName(change.kind));
        ImGui::TableNextColumn();
        if (change.commandsBefore != change.commandsAfter) {
            ImGui::Text("%zu → %zu", change.commandsBefore, change.commandsAfter);
        } else if (change.commandsAfter > 0) {
            ImGui::TextColored(core::palette::TextMuted, "%zu", change.commandsAfter);
        }
        ImGui::TableNextColumn();
        if (change.byteDelta != 0) {
            ImGui::Text("%+lld", static_cast<long long>(change.byteDelta));
        }
    }
    ImGui::EndTable();
}

} // namespace toki::scene
