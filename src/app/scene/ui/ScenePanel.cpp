#include "ScenePanel.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] glm::vec3 makePoint(const std::array<s16, 3>& position) {
    return {position[0], position[1], position[2]};
}

} // namespace

ScenePanel::ScenePanel(const workspace::WorkspaceService& workspaceService,
                       viewport::ViewportPanel& viewportPanel)
    : Panel("scene", "Scene", core::icons::MapTrifold, core::DockSlot::Right),
      workspaceService(workspaceService), viewportPanel(viewportPanel) {}

void ScenePanel::setScene(Scene read) {
    const bool isNew = !scene || !core::isSamePath(scene->directory, read.directory);
    const bool isRestored =
        isNew && pendingSession && core::isSamePath(pendingSession->directory, read.directory);
    if (isRestored) {
        sceneView = restoreSceneView(read, *pendingSession);
    } else {
        sceneView = isNew ? makeSceneView(read) : keepSceneView(read, std::move(sceneView));
    }
    if (animatesActors && !sceneView.actorFrame) {
        sceneView.actorFrame = 0;
    }
    if (!isNew) {
        read.actors = std::move(scene->actors);
    }
    scene = std::move(read);

    std::optional<glm::vec3> target;
    if (isNew && !isRestored && !scene->layers.empty() &&
        !scene->layers.front().header.playerEntries.empty()) {
        target = makePoint(scene->layers.front().header.playerEntries.front().position);
    }
    viewportPanel.setContent(makeSceneContent(*scene, sceneView), target);
    if (isRestored && pendingSession->camera) {
        viewportPanel.setCameraPose(*pendingSession->camera);
    }
    if (isNew) {
        pendingSession.reset();
    }
    if (isNew) {
        viewportPanel.setOpen(true);
        setOpen(true);
    }
}

std::optional<core::SceneSession> ScenePanel::makeSession() const {
    if (!scene || sceneView.layer >= scene->layers.size()) {
        return std::nullopt;
    }
    core::SceneSession session = makeSceneSession(*scene, sceneView);
    session.camera = viewportPanel.getCameraPose();
    return session;
}

void ScenePanel::showScene() {
    if (scene) {
        viewportPanel.setContent(makeSceneContent(*scene, sceneView));
    }
}

void ScenePanel::setActors(SceneActors actors) {
    if (scene) {
        scene->actors = std::move(actors);
        showScene();
    }
}

void ScenePanel::setAnimatesActors(const bool animates) {
    animatesActors = animates;
    sceneView.actorFrame = animates ? std::optional<usize>(0) : std::nullopt;
    frameTime = 0.0F;
    showScene();
}

void ScenePanel::advanceActors() {
    constexpr f32 SecondsPerFrame = 1.0F / 20.0F;
    if (!animatesActors || !scene || !hasAnimatedActors(scene->actors)) {
        return;
    }
    frameTime += ImGui::GetIO().DeltaTime;
    if (frameTime < SecondsPerFrame) {
        return;
    }
    usize frame = sceneView.actorFrame.value_or(0);
    while (frameTime >= SecondsPerFrame) {
        frameTime -= SecondsPerFrame;
        ++frame;
    }
    sceneView.actorFrame = frame;
    viewportPanel.setNextFrame(makeSceneContent(*scene, sceneView));
}

void ScenePanel::draw() {
    advanceActors();
    if (!scene || scene->layers.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "Open a scene in the Assets panel.");
        return;
    }
    const Scene& shown = *scene;
    ImGui::TextUnformatted(shown.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%zu rooms, %zu layers",
                       shown.layers.front().rooms.size(), shown.layers.size());
    ImGui::Separator();
    drawPickers(shown);
    const SceneLayer& layer = shown.layers[sceneView.layer];
    drawRooms(layer);
    drawPlacements(layer);
}

void ScenePanel::drawPickers(const Scene& shown) {
    const SceneLayer& current = shown.layers[sceneView.layer];
    if (ImGui::BeginCombo("Layer", makeLayerName(current.index).c_str())) {
        for (usize i = 0; i < shown.layers.size(); ++i) {
            const std::string name = makeLayerName(shown.layers[i].index);
            if (ImGui::Selectable(name.c_str(), i == sceneView.layer) && i != sceneView.layer) {
                sceneView.layer = i;
                sceneView = keepSceneView(shown, std::move(sceneView));
                showScene();
            }
        }
        ImGui::EndCombo();
    }

    const SceneHeader& header = shown.layers[sceneView.layer].header;
    const std::string light = sceneView.lightSetting < header.lightSettings.size()
                                  ? makeLightSettingName(header, sceneView.lightSetting)
                                  : std::string("none");
    if (ImGui::BeginCombo("Light", light.c_str())) {
        for (usize i = 0; i < header.lightSettings.size(); ++i) {
            const std::string name = makeLightSettingName(header, i);
            if (ImGui::Selectable(name.c_str(), i == sceneView.lightSetting)) {
                sceneView.lightSetting = i;
                showScene();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Show actors", &sceneView.showsActors)) {
        showScene();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!sceneView.showsActors);
    if (ImGui::Checkbox("Draw models", &sceneView.drawsModels)) {
        showScene();
    }
    ImGui::EndDisabled();
    const auto drawn = std::ranges::count_if(
        shown.actors.models, [](const auto& entry) { return entry.second.hasDrawing(); });
    ImGui::TextColored(core::palette::TextMuted, "%d of %d kinds of actor drawn as models",
                       static_cast<int>(drawn), static_cast<int>(shown.actors.models.size()));
}

void ScenePanel::drawRooms(const SceneLayer& layer) {
    if (!ImGui::CollapsingHeader("Rooms", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (usize room = 0; room < layer.rooms.size(); ++room) {
        ImGui::PushID(static_cast<int>(room));
        bool shown = room >= sceneView.hiddenRooms.size() || !sceneView.hiddenRooms[room];
        if (ImGui::Checkbox("##shown", &shown) && room < sceneView.hiddenRooms.size()) {
            sceneView.hiddenRooms[room] = !shown;
            showScene();
        }
        ImGui::SameLine();
        const RoomHeader& header = layer.rooms[room];
        const std::string label = std::format("Room {}  ·  {} meshes, {} actors", room,
                                              header.displayLists.size(), header.actors.size());
        if (ImGui::TreeNode(label.c_str())) {
            const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
            const std::vector<std::string> loaded =
                workspace != nullptr ? listLoadedObjects(layer, room, workspace->getTables())
                                     : std::vector<std::string>{};
            for (const ActorPlacement& actor : header.actors) {
                const actor::PlacementValues placement{.params = static_cast<u16>(actor.params),
                                                       .rotation = actor.rotation,
                                                       .age = findLayerAge(layer.index)};
                static_cast<void>(drawActor(
                    actor.id, actor.position, placement, nullptr,
                    workspace != nullptr ? std::optional<std::span<const std::string>>(loaded)
                                         : std::nullopt));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void ScenePanel::drawPlacements(const SceneLayer& layer) {
    if (ImGui::CollapsingHeader("Entrances")) {
        for (usize i = 0; i < layer.header.playerEntries.size(); ++i) {
            const ActorPlacement& entry = layer.header.playerEntries[i];
            const std::string label =
                std::format("Entry {}{}", i, i == sceneView.shownEntrance ? "  ·  Link" : "");
            const actor::PlacementValues placement{.params = static_cast<u16>(entry.params),
                                                   .rotation = entry.rotation,
                                                   .age = findLayerAge(layer.index)};
            if (drawActor(entry.id, entry.position, placement, label.c_str(), std::nullopt)) {
                sceneView.shownEntrance = i;
                showScene();
            }
        }
    }
    if (ImGui::CollapsingHeader("Transition actors")) {
        for (const TransitionActorPlacement& actor : layer.header.transitionActors) {
            const std::string label = std::format("{}  ·  rooms {} and {}", findActorName(actor.id),
                                                  actor.rooms[0], actor.rooms[1]);
            const actor::PlacementValues placement{.params = static_cast<u16>(actor.params),
                                                   .rotation = {0, actor.rotationY, 0},
                                                   .age = findLayerAge(layer.index)};
            static_cast<void>(
                drawActor(actor.id, actor.position, placement, label.c_str(), std::nullopt));
        }
    }
}

bool ScenePanel::drawActor(const s16 actorId, const std::array<s16, 3>& position,
                           const actor::PlacementValues& placement, const char* label,
                           const std::optional<std::span<const std::string>> loadedObjects) {
    const auto model = scene->actors.models.find(actorId);
    const ActorDrawing* drawing =
        model != scene->actors.models.end() ? model->second.findDrawing(placement) : nullptr;
    const bool isDrawn = drawing != nullptr && drawing->isDrawn();
    ImGui::TextColored(isDrawn ? core::palette::Success : core::palette::TextDisabled, "%s",
                       isDrawn ? core::icons::Cube : core::icons::Diamond);
    ImGui::SameLine();
    const std::string name = label != nullptr ? std::string(label) : findActorName(actorId);
    const std::string text =
        std::format("{}##{}_{}_{}_{}", name, actorId, position[0], position[1], position[2]);
    const bool isClicked = ImGui::Selectable(text.c_str());
    if (isClicked) {
        viewportPanel.focus(makePoint(position));
    }
    if (ImGui::BeginItemTooltip()) {
        drawActorTooltip(actorId, position, placement, loadedObjects);
        ImGui::EndTooltip();
    }
    return isClicked;
}

void ScenePanel::drawActorTooltip(
    const s16 actorId, const std::array<s16, 3>& position, const actor::PlacementValues& placement,
    const std::optional<std::span<const std::string>> loadedObjects) const {
    ImGui::Text("actor 0x%04X at %d, %d, %d — click to look at it",
                static_cast<unsigned>(static_cast<u16>(actorId)), position[0], position[1],
                position[2]);
    const auto model = scene->actors.models.find(actorId);
    if (model == scene->actors.models.end()) {
        return;
    }
    const ActorDescription description = describeActor(model->second, placement, loadedObjects);
    ImGui::Separator();
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       description.isDrawn ? "Drawn as its model" : "Marked: no model to draw");
    for (const std::string& line : description.lines) {
        ImGui::TextUnformatted(line.c_str());
    }
    for (const std::string& problem : description.problems) {
        ImGui::TextColored(core::palette::Warning, "%s %s", core::icons::Warning, problem.c_str());
    }
}

std::string ScenePanel::findActorName(const s16 actorId) const {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    const auto index = static_cast<usize>(static_cast<u16>(actorId));
    if (workspace != nullptr && index < workspace->getTables().actors.size()) {
        const hackeroot::ActorEntry& entry = workspace->getTables().actors[index];
        return entry.name.empty() ? entry.enumName : entry.name;
    }
    return std::format("actor 0x{:04X}", index);
}

} // namespace toki::scene
