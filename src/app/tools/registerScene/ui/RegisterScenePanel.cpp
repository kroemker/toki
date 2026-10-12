#include "RegisterScenePanel.h"

#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/tools/Types.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/tools/registerScene/domain/SceneRegistry.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::tools {

namespace {

constexpr float ListWidth = 14.0F;
constexpr int MaxSpawns = 32;

[[nodiscard]] bool isRegistered(const hackeroot::Workspace& workspace, const std::string& name) {
    return std::ranges::contains(workspace.getTables().scenes, name + "_scene",
                                 &hackeroot::SceneEntry::name);
}

[[nodiscard]] const hackeroot::AssetDirectory* findScene(const hackeroot::Workspace& workspace,
                                                         const std::string& name) {
    const auto& scenes = workspace.getAssets().scenes;
    const auto found = std::ranges::find(scenes, name, &hackeroot::AssetDirectory::name);
    return found == scenes.end() || found->isExtracted ? nullptr : &*found;
}

} // namespace

RegisterScenePanel::RegisterScenePanel(const workspace::WorkspaceService& workspaceService,
                                       actor::CollectionCatalog& collectionCatalog)
    : Panel("registerScene", "Register Scene", core::icons::MapTrifold, core::DockSlot::Right),
      workspaceService(workspaceService), collectionCatalog(collectionCatalog) {}

void RegisterScenePanel::select(const hackeroot::AssetDirectory& scene) {
    selected = scene.name;
    registration = SceneRegistration{.mapSelectLabel = makeMapSelectLabel(scene.name)};
}

void RegisterScenePanel::draw() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Open a checkout to register its scenes.");
        return;
    }
    ImGui::TextColored(core::palette::TextMuted,
                       "Makes a scene fast64 exported to assets/scenes/ part of the build.");
    ImGui::BeginDisabled(editFlow.isReviewing() || editFlow.isBusy());
    drawScenes(*workspace);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::BeginChild("##options")) {
        drawOptions(*workspace);
    }
    ImGui::EndChild();
}

void RegisterScenePanel::drawScenes(const hackeroot::Workspace& workspace) {
    if (ImGui::BeginChild("##scenes", ImVec2(ImGui::GetFontSize() * ListWidth, 0.0F),
                          ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
        bool isAny = false;
        for (const hackeroot::AssetDirectory& scene : workspace.getAssets().scenes) {
            if (scene.isExtracted) {
                continue;
            }
            isAny = true;
            const bool isDone = isRegistered(workspace, scene.name);
            ImGui::TextColored(isDone ? core::palette::Success : core::palette::TextDisabled, "%s",
                               isDone ? core::icons::CheckCircle : core::icons::Circle);
            ImGui::SameLine();
            if (ImGui::Selectable(scene.name.c_str(), selected == scene.name)) {
                select(scene);
            }
            ImGui::SetItemTooltip("%s  ·  %zu rooms", isDone ? "Registered" : "Not registered",
                                  scene.roomCount);
        }
        if (!isAny) {
            ImGui::TextColored(core::palette::TextMuted, "No scene folders in assets/scenes/.");
        }
    }
    ImGui::EndChild();
}

void RegisterScenePanel::drawOptions(const hackeroot::Workspace& workspace) {
    const hackeroot::AssetDirectory* scene = selected ? findScene(workspace, *selected) : nullptr;
    if (scene == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Pick a scene to register.");
        return;
    }
    ImGui::TextUnformatted(scene->name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%s  ·  %zu rooms",
                       isRegistered(workspace, scene->name) ? "registered" : "not registered",
                       scene->roomCount);

    ImGui::BeginDisabled(editFlow.isReviewing() || editFlow.isBusy());
    std::vector<std::string_view> drawConfigs;
    if (const actor::ValueCollection* configs =
            collectionCatalog.findSharedCollection("SceneDrawConfig")) {
        for (const actor::ChoiceValue& config : configs->values) {
            drawConfigs.push_back(config.label);
        }
    }
    static_cast<void>(drawNames("Draw config", registration.drawConfig, drawConfigs));
    bool isAllSpawns = !registration.spawnCount;
    if (ImGui::Checkbox("Every spawn the scene has", &isAllSpawns)) {
        registration.spawnCount = isAllSpawns ? std::nullopt : std::optional<usize>(1);
    }
    if (registration.spawnCount) {
        int spawns = static_cast<int>(*registration.spawnCount);
        if (ImGui::InputInt("Spawns", &spawns)) {
            registration.spawnCount = static_cast<usize>(std::clamp(spawns, 1, MaxSpawns));
        }
    }
    ImGui::Checkbox("List it in the map select", &registration.addsMapSelect);
    if (registration.addsMapSelect) {
        ImGui::InputText("As", &registration.mapSelectLabel);
    }
    ImGui::Checkbox("Boot into it", &registration.bootsToScene);
    if (registration.bootsToScene) {
        int spawn = static_cast<int>(registration.bootSpawn);
        if (ImGui::InputInt("At spawn", &spawn)) {
            registration.bootSpawn = static_cast<usize>(std::clamp(spawn, 0, MaxSpawns - 1));
        }
        drawOptionalNames("Age", registration.bootAge, findListedOptions("LINK_AGE_"));
        drawOptionalNames("Time", registration.bootTime, findListedOptions("NEXT_TIME_"));
    }

    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::BeginDisabled(editFlow.isBusy() || editFlow.isReviewing());
    if (ImGui::Button("Review")) {
        editFlow.requestPlan(
            workspace.getRoot(),
            [shared = workspaceService.getSharedWorkspace(), asset = *scene,
             wanted = registration]() -> std::expected<hackeroot::EditPlan, std::string> {
                if (shared == nullptr) {
                    return std::unexpected(std::string("no checkout is open"));
                }
                const std::expected<SurveyedScene, std::string> surveyed =
                    surveyScene(shared, asset);
                if (!surveyed) {
                    return std::unexpected(surveyed.error());
                }
                return makeScenePlan(shared->getRoot(), surveyed->scene, wanted, surveyed->survey);
            });
    }
    ImGui::EndDisabled();
    static_cast<void>(editFlow.draw(workspace.getRoot()));
}

bool RegisterScenePanel::drawNames(const char* label, std::string& value,
                                   const std::span<const std::string_view> names) {
    if (!ImGui::BeginCombo(label, value.c_str())) {
        return false;
    }
    bool isChanged = false;
    for (const std::string_view name : names) {
        if (ImGui::Selectable(std::string(name).c_str(), name == value)) {
            isChanged = name != value;
            value = std::string(name);
        }
    }
    ImGui::EndCombo();
    return isChanged;
}

void RegisterScenePanel::drawOptionalNames(const char* label, std::optional<std::string>& value,
                                           const std::span<const std::string_view> names) {
    constexpr const char* Unchanged = "As configured";
    if (!ImGui::BeginCombo(label, value ? value->c_str() : Unchanged)) {
        return;
    }
    if (ImGui::Selectable(Unchanged, !value)) {
        value.reset();
    }
    for (const std::string_view name : names) {
        if (ImGui::Selectable(std::string(name).c_str(), value == name)) {
            value = std::string(name);
        }
    }
    ImGui::EndCombo();
}

} // namespace toki::tools
