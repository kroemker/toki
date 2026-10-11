#include "ScenePanel.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/DatabaseEdits.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/ParamReading.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneFlags.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr float DimmedLinkAlpha = 0.2F;
constexpr glm::vec3 LinkLift{0.0F, 30.0F, 0.0F};

[[nodiscard]] ImVec4 findFlagColor(const actor::SceneFlag kind) {
    switch (kind) {
    case actor::SceneFlag::Switch:
        return core::palette::Warning;
    case actor::SceneFlag::Treasure:
        return ImVec4(1.0F, 0.6F, 0.25F, 1.0F);
    case actor::SceneFlag::Collectible:
        return core::palette::Success;
    case actor::SceneFlag::Clear:
        return core::palette::Danger;
    }
    return core::palette::Text;
}

[[nodiscard]] glm::vec3 makePoint(const std::array<s16, 3>& position) {
    return {position[0], position[1], position[2]};
}

} // namespace

ScenePanel::ScenePanel(const workspace::WorkspaceService& workspaceService,
                       viewport::ViewportPanel& viewportPanel, actor::DatabaseStore& databaseStore,
                       actor::CollectionCatalog& collectionCatalog)
    : Panel("scene", "Scene", core::icons::MapTrifold, core::DockSlot::Right),
      workspaceService(workspaceService), viewportPanel(viewportPanel),
      databaseStore(databaseStore), collectionCatalog(collectionCatalog) {}

void ScenePanel::setScene(Scene read) {
    const bool isNew = !scene || !core::isSamePath(scene->directory, read.directory);
    const bool isRestored =
        isNew && pendingSession && core::isSamePath(pendingSession->directory, read.directory);
    if (isRestored) {
        sceneView = restoreSceneView(read, *pendingSession);
    } else {
        sceneView = isNew ? makeSceneView(read) : keepSceneView(read, std::move(sceneView));
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

void ScenePanel::draw() {
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
    const std::vector<SceneFlagUse> flags = listSceneFlags(layer, scene->actors);
    drawFlags(flags);
    drawFlagLinks(flags);
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
                const actor::PlacementValues placement =
                    makePlacementValues(scene->actors, static_cast<u16>(actor.params),
                                        actor.rotation, findLayerAge(layer.index));
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
            const actor::PlacementValues placement =
                makePlacementValues(scene->actors, static_cast<u16>(entry.params), entry.rotation,
                                    findLayerAge(layer.index));
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
            const actor::PlacementValues placement =
                makePlacementValues(scene->actors, static_cast<u16>(actor.params),
                                    {0, actor.rotationY, 0}, findLayerAge(layer.index));
            static_cast<void>(
                drawActor(actor.id, actor.position, placement, label.c_str(), std::nullopt));
        }
    }
}

bool ScenePanel::drawActor(const s16 actorId, const std::array<s16, 3>& position,
                           const actor::PlacementValues& placement, const char* label,
                           const std::optional<std::span<const std::string>> loadedObjects) {
    const auto model = scene->actors.models.find(actorId);
    const std::optional<ActorDescription> description =
        model != scene->actors.models.end()
            ? std::optional(describeActor(model->second, placement, loadedObjects,
                                          [this, &model](const std::string_view name) {
                                              return collectionCatalog.findCollection(
                                                  model->second.entry->actor, name);
                                          }))
            : std::nullopt;
    const bool isDrawn = description && description->isDrawn;
    ImGui::TextColored(isDrawn ? core::palette::Success : core::palette::TextDisabled, "%s",
                       isDrawn ? core::icons::Cube : core::icons::Diamond);
    ImGui::SameLine();
    std::string name = findActorName(actorId);
    if (label != nullptr) {
        name = label;
    } else if (description && !description->name.empty()) {
        name = description->name;
    }
    const std::string text =
        std::format("{}##{}_{}_{}_{}", name, actorId, position[0], position[1], position[2]);
    const bool isClicked = ImGui::Selectable(text.c_str());
    if (isClicked) {
        viewportPanel.focus(makePoint(position));
    }
    if (!description) {
        return isClicked;
    }
    if (ImGui::BeginItemTooltip()) {
        drawActorTooltip(actorId, position, *description);
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem()) {
        drawActorMenu(model->second, placement, *description);
        ImGui::EndPopup();
    }
    return isClicked;
}

void ScenePanel::drawActorTooltip(const s16 actorId, const std::array<s16, 3>& position,
                                  const ActorDescription& description) const {
    ImGui::Text("%s  ·  actor 0x%04X at %d, %d, %d", findActorName(actorId).c_str(),
                static_cast<unsigned>(static_cast<u16>(actorId)), position[0], position[1],
                position[2]);
    ImGui::TextColored(core::palette::TextMuted, "%s  ·  click to look at it, right-click to edit",
                       description.isDrawn ? "Drawn as its model" : "Marked");
    if (!description.params.empty() &&
        ImGui::BeginTable("params", 3, ImGuiTableFlags_SizingFixedFit)) {
        for (const actor::ParamReading& param : description.params) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(core::palette::TextMuted, "%s", param.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(param.text.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(core::palette::TextMuted, "%s", param.description.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    for (const std::string& line : description.lines) {
        ImGui::TextUnformatted(line.c_str());
    }
    for (const std::string& problem : description.problems) {
        ImGui::TextColored(core::palette::Warning, "%s %s", core::icons::Warning, problem.c_str());
    }
}

void ScenePanel::drawActorMenu(const SceneActorModel& model,
                               const actor::PlacementValues& placement,
                               const ActorDescription& description) {
    if (ImGui::MenuItem("Add a variant for these params", nullptr, false,
                        model.entry && description.canAddVariant)) {
        databaseStore.edit([entry = *model.entry, placement](actor::ActorDatabase& checkout) {
            return actor::addVariantForParams(checkout, entry, placement);
        });
    }
    if (ImGui::MenuItem("Edit in the Actor Database", nullptr, false, openActorEditor != nullptr)) {
        openActorEditor(model.enumName);
    }
}

void ScenePanel::drawFlags(const std::vector<SceneFlagUse>& flags) {
    hoveredFlag.reset();
    const std::string title = std::format("Flags ({})###flags", flags.size());
    if (!ImGui::CollapsingHeader(title.c_str())) {
        return;
    }
    ImGui::Checkbox("Show links in the viewport", &showsFlagLinks);
    ImGui::SetItemTooltip("A line from each actor that sets a flag to each that reads it; "
                          "hover a flag to see only its own");
    if (flags.empty()) {
        ImGui::TextColored(core::palette::TextMuted,
                           "No placed actor's entry names a scene flag in its params.");
        return;
    }
    for (const SceneFlagUse& flag : flags) {
        ImGui::PushID(static_cast<int>(flag.kind));
        ImGui::PushID(static_cast<int>(flag.number));
        const std::string label = std::format(
            "{} 0x{:02X}  ·  {}  ·  {} {}", actor::getEntryLabel(actor::SceneFlagNames, flag.kind),
            flag.number, isSavedSceneFlag(flag.kind, flag.number) ? "saved" : "reset on leaving",
            flag.users.size(), flag.users.size() == 1 ? "actor" : "actors");
        const bool isOpen = ImGui::TreeNode("##flag", "%s", label.c_str());
        if (ImGui::IsItemHovered()) {
            hoveredFlag = std::pair(flag.kind, flag.number);
        }
        if (!flag.isSet() || !flag.isRead()) {
            ImGui::SameLine();
            ImGui::TextColored(core::palette::TextMuted, "%s",
                               flag.isSet() ? "read by none here" : "set elsewhere");
            ImGui::SetItemTooltip("%s", flag.isSet()
                                            ? "No actor placed in this layer reads it"
                                            : "Set by no actor placed in this layer: by a "
                                              "cutscene, another scene or the player, perhaps");
        }
        if (isOpen) {
            for (usize i = 0; i < flag.users.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                drawFlagUser(flag.users[i]);
                if (ImGui::IsItemHovered()) {
                    hoveredFlag = std::pair(flag.kind, flag.number);
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

void ScenePanel::drawFlagUser(const FlagUser& user) {
    const char* icon = core::icons::Eye;
    if (user.use == actor::FlagUse::Sets) {
        icon = core::icons::Lightning;
    } else if (user.use == actor::FlagUse::Both) {
        icon = core::icons::ArrowsLeftRight;
    }
    ImGui::TextColored(user.use == actor::FlagUse::Reads ? core::palette::TextMuted
                                                         : core::palette::Warning,
                       "%s", icon);
    ImGui::SameLine();
    const auto model = scene->actors.models.find(user.actorId);
    const std::string name = model != scene->actors.models.end() && model->second.entry &&
                                     !model->second.entry->name.empty()
                                 ? model->second.entry->name
                                 : findActorName(user.actorId);
    const std::string where =
        user.room ? std::format("room {}", *user.room) : std::string("between rooms");
    const std::string text = std::format(
        "{}  ·  {}, {}", name, actor::getEntryLabel(actor::FlagUseNames, user.use), where);
    if (ImGui::Selectable(text.c_str())) {
        viewportPanel.focus(makePoint(user.position));
    }
    ImGui::SetItemTooltip("By its param \"%s\"", user.param.c_str());
}

void ScenePanel::drawFlagLinks(const std::vector<SceneFlagUse>& flags) {
    viewport::ViewportOverlay overlay;
    if (!showsFlagLinks || !sceneView.showsActors) {
        viewportPanel.setOverlay(std::move(overlay));
        return;
    }
    const auto isShown = [this](const FlagUser& user) {
        return !user.room || *user.room >= sceneView.hiddenRooms.size() ||
               !sceneView.hiddenRooms[*user.room];
    };
    for (const SceneFlagUse& flag : flags) {
        SceneFlagUse shown{.kind = flag.kind, .number = flag.number};
        std::ranges::copy_if(flag.users, std::back_inserter(shown.users), isShown);
        const bool isDimmed = hoveredFlag && *hoveredFlag != std::pair(flag.kind, flag.number);
        ImVec4 color = findFlagColor(flag.kind);
        color.w = isDimmed ? DimmedLinkAlpha : 1.0F;
        for (const FlagLink& link : listFlagLinks(shown)) {
            overlay.lines.push_back(viewport::OverlayLine{
                .from = makePoint(shown.users[link.setter].position) + LinkLift,
                .to = makePoint(shown.users[link.reader].position) + LinkLift,
                .color = ImGui::GetColorU32(color),
                .hasArrowAtFrom = link.isMutual,
                .hasArrowAtTo = true});
        }
    }
    viewportPanel.setOverlay(std::move(overlay));
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
