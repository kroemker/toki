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
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] viewport::ActorDisplay findActorDisplay(const SceneView& view) {
    if (!view.showsActors) {
        return viewport::ActorDisplay::Hidden;
    }
    return view.drawsModels ? viewport::ActorDisplay::Models : viewport::ActorDisplay::Markers;
}

[[nodiscard]] glm::vec3 makePoint(const std::array<s16, 3>& position) {
    return {position[0], position[1], position[2]};
}

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

[[nodiscard]] std::array<s16, 3> makePosition(const glm::vec3& point) {
    return {roundToS16(point.x), roundToS16(point.y), roundToS16(point.z)};
}

[[nodiscard]] std::optional<std::string> findSymbolName(const z64::MemoryImage& image,
                                                        const u32 address) {
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.segment == (address >> 24) && symbol.offset == (address & 0x00FF'FFFF)) {
            return name;
        }
    }
    return std::nullopt;
}

} // namespace

ScenePanel::ScenePanel(const workspace::WorkspaceService& workspaceService,
                       viewport::ViewportPanel& viewportPanel, actor::DatabaseStore& databaseStore,
                       actor::CollectionCatalog& collectionCatalog)
    : Panel("scene", "Scene", core::icons::MapTrifold, core::DockSlot::Right),
      workspaceService(workspaceService), viewportPanel(viewportPanel),
      databaseStore(databaseStore), collectionCatalog(collectionCatalog),
      placementEditor(workspaceService, [this](const bool hasNewActors) {
          showScene();
          if (hasNewActors && readActors && scene) {
              readActors(*scene);
          }
      }) {}

void ScenePanel::setScene(Scene read) {
    if (scene && placementEditor.isEdited()) {
        heldScene = std::move(read);
        return;
    }
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
    placementEditor.refresh(*scene);

    std::optional<glm::vec3> target;
    if (isNew && !isRestored && !scene->layers.empty() &&
        !scene->layers.front().header.playerEntries.empty()) {
        target = makePoint(scene->layers.front().header.playerEntries.front().position);
    }
    viewportPanel.setContent(makeSceneContent(*scene, sceneView, markerShape), target);
    offerChoices();
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
        viewportPanel.setContent(makeSceneContent(*scene, sceneView, markerShape));
        offerChoices();
    }
}

void ScenePanel::offerChoices() {
    if (!scene || sceneView.layer >= scene->layers.size()) {
        viewportPanel.clearChoices();
        return;
    }
    const SceneHeader& header = scene->layers[sceneView.layer].header;
    viewport::ViewportChoices choices{.lightSetting = sceneView.lightSetting,
                                      .actors = findActorDisplay(sceneView)};
    for (usize i = 0; i < header.lightSettings.size(); ++i) {
        choices.lightSettings.push_back(makeLightSettingName(header, i));
    }
    viewportPanel.setChoices(std::move(choices), [this](const viewport::ViewportChoices& chosen) {
        sceneView.lightSetting = chosen.lightSetting;
        const viewport::ActorDisplay actors =
            chosen.actors.value_or(viewport::ActorDisplay::Models);
        sceneView.showsActors = actors != viewport::ActorDisplay::Hidden;
        if (sceneView.showsActors) {
            sceneView.drawsModels = actors == viewport::ActorDisplay::Models;
        }
        showScene();
    });
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
    drawHeldScene();
    const Scene& shown = *scene;
    ImGui::TextUnformatted(shown.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%zu rooms, %zu layers",
                       shown.layers.front().rooms.size(), shown.layers.size());
    if (placementEditor.isEdited()) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::Warning, "%s unsaved", core::icons::Circle);
        ImGui::SetItemTooltip("Edited since it was read. File > Save Scene writes it.");
    }
    if (!placementEditor.getFailure().empty()) {
        ImGui::TextColored(core::palette::Danger, "%s %s", core::icons::Warning,
                           placementEditor.getFailure().c_str());
    }
    ImGui::Separator();
    drawPickers(shown);
    placementEditor.drawInspector(*scene);
    const SceneLayer& layer = scene->layers[sceneView.layer];
    drawRooms(layer);
    drawPlacements(layer);
    const std::vector<SceneFlagUse> flags = listSceneFlags(layer, scene->actors);
    drawFlags(flags);
    drawFlagLinks(flags);
    offerPlacements(layer);
    placementEditor.applyPicked(*scene);
}

void ScenePanel::offerPlacements(const SceneLayer& layer) {
    std::vector<glm::vec3> points;
    std::vector<PlacementRef> placements;
    const auto offer = [&points, &placements](const PlacementRef& where,
                                              const std::array<s16, 3>& position) {
        points.push_back(makePoint(position));
        placements.push_back(where);
    };
    if (sceneView.showsActors) {
        for (usize room = 0; room < layer.rooms.size(); ++room) {
            if (room < sceneView.hiddenRooms.size() && sceneView.hiddenRooms[room]) {
                continue;
            }
            for (usize i = 0; i < layer.rooms[room].actors.size(); ++i) {
                offer({.kind = PlacementKind::Actor,
                       .layer = sceneView.layer,
                       .room = room,
                       .index = i},
                      layer.rooms[room].actors[i].position);
            }
        }
        for (usize i = 0; i < layer.header.transitionActors.size(); ++i) {
            offer({.kind = PlacementKind::TransitionActor, .layer = sceneView.layer, .index = i},
                  layer.header.transitionActors[i].position);
        }
        for (usize i = 0; i < layer.header.playerEntries.size(); ++i) {
            offer({.kind = PlacementKind::PlayerEntry, .layer = sceneView.layer, .index = i},
                  layer.header.playerEntries[i].position);
        }
    }
    viewportPanel.setPickables(std::move(points),
                               [this, placements = std::move(placements)](const usize index) {
                                   placementEditor.select(placements[index]);
                               });
    const std::optional<std::array<s16, 3>> selected = placementEditor.findSelectedPosition(*scene);
    viewportPanel.setHandle(selected ? std::optional(makePoint(*selected)) : std::nullopt,
                            [this](const glm::vec3& moved, const bool isDone) {
                                if (scene) {
                                    placementEditor.moveSelected(*scene, makePosition(moved),
                                                                 isDone);
                                }
                            });
}

void ScenePanel::drawHeldScene() {
    constexpr const char* Title = "Unsaved Edits###heldScene";
    if (heldScene && !ImGui::IsPopupOpen(Title)) {
        ImGui::OpenPopup(Title);
    }
    if (!ImGui::BeginPopupModal(Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!heldScene) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const bool isSame = core::isSamePath(heldScene->directory, scene->directory);
    ImGui::TextUnformatted(
        isSame ? std::format("{}'s files changed on disk while it has edits no save wrote.",
                             scene->name)
                     .c_str()
               : std::format("{} has edits no save wrote, and {} is to be opened.", scene->name,
                             heldScene->name)
                     .c_str());
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       isSame ? "Reading it again throws the edits away."
                              : "Opening it throws the edits away.");
    if (ImGui::Button(isSame ? "Read It Again" : "Open It")) {
        placementEditor.reset();
        Scene read = std::move(*heldScene);
        heldScene.reset();
        ImGui::CloseCurrentPopup();
        setScene(std::move(read));
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep the Edits")) {
        heldScene.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
        const bool isRoomOpen = ImGui::TreeNode(label.c_str());
        drawRoomMenu(room, header);
        if (isRoomOpen) {
            const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
            const std::vector<std::string> loaded =
                workspace != nullptr ? listLoadedObjects(layer, room, workspace->getTables())
                                     : std::vector<std::string>{};
            for (usize i = 0; i < header.actors.size(); ++i) {
                const ActorPlacement& actor = header.actors[i];
                const actor::PlacementValues placement =
                    makePlacementValues(scene->actors, static_cast<u16>(actor.params),
                                        actor.rotation, findLayerAge(layer.index));
                ImGui::PushID(static_cast<int>(i));
                static_cast<void>(drawActor(
                    PlacementRef{.kind = PlacementKind::Actor,
                                 .layer = sceneView.layer,
                                 .room = room,
                                 .index = i},
                    actor.id, actor.position, placement, nullptr,
                    workspace != nullptr ? std::optional<std::span<const std::string>>(loaded)
                                         : std::nullopt));
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void ScenePanel::drawRoomMenu(const usize room, const RoomHeader& header) {
    if (!scene || !ImGui::BeginPopupContextItem()) {
        return;
    }
    const core::CameraPose camera = viewportPanel.getCameraPose();
    placementEditor.drawRoomMenu(sceneView.layer, room,
                                 {static_cast<s16>(camera.target[0]),
                                  static_cast<s16>(camera.target[1]),
                                  static_cast<s16>(camera.target[2])});
    if (inspectDisplayList == nullptr) {
        ImGui::EndPopup();
        return;
    }
    ImGui::Separator();
    const usize image = FirstRoomImage + room;
    for (const RoomDisplayLists& mesh : header.displayLists) {
        for (const u32 address : {mesh.opaque, mesh.translucent}) {
            const std::optional<std::string> name =
                address != 0 && image < scene->images.size()
                    ? findSymbolName(*scene->images[image], address)
                    : std::nullopt;
            if (name && ImGui::MenuItem(std::format("Inspect {}", *name).c_str())) {
                inspectDisplayList(*name);
            }
        }
    }
    ImGui::EndPopup();
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
            ImGui::PushID(static_cast<int>(i));
            const bool isClicked = drawActor(
                PlacementRef{
                    .kind = PlacementKind::PlayerEntry, .layer = sceneView.layer, .index = i},
                entry.id, entry.position, placement, label.c_str(), std::nullopt);
            ImGui::PopID();
            if (isClicked) {
                sceneView.shownEntrance = i;
                showScene();
            }
        }
    }
    if (ImGui::CollapsingHeader("Transition actors")) {
        for (usize i = 0; i < layer.header.transitionActors.size(); ++i) {
            const TransitionActorPlacement& actor = layer.header.transitionActors[i];
            const std::string label = std::format("{}  ·  rooms {} and {}", findActorName(actor.id),
                                                  actor.rooms[0], actor.rooms[1]);
            const actor::PlacementValues placement =
                makePlacementValues(scene->actors, static_cast<u16>(actor.params),
                                    {0, actor.rotationY, 0}, findLayerAge(layer.index));
            ImGui::PushID(static_cast<int>(i));
            static_cast<void>(drawActor(
                PlacementRef{
                    .kind = PlacementKind::TransitionActor, .layer = sceneView.layer, .index = i},
                actor.id, actor.position, placement, label.c_str(), std::nullopt));
            ImGui::PopID();
        }
    }
}

bool ScenePanel::drawActor(const PlacementRef& where, const s16 actorId,
                           const std::array<s16, 3>& position,
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
    const bool isClicked = ImGui::Selectable(name.c_str(), placementEditor.isSelected(where));
    if (isClicked) {
        placementEditor.select(where);
        viewportPanel.focus(makePoint(position));
    }
    if (description && ImGui::BeginItemTooltip()) {
        drawActorTooltip(actorId, position, *description);
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem()) {
        if (description) {
            drawActorMenu(model->second, placement, *description);
        }
        placementEditor.drawPlacementMenu(*scene, where);
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
    if (ImGui::MenuItem("Add an adjustment for these params", nullptr, false,
                        model.entry && description.canAddVariant)) {
        databaseStore.edit([entry = *model.entry, placement](actor::ActorDatabase& checkout) {
            return actor::addAdjustmentForParams(checkout, entry, placement);
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
    if (!sceneView.showsActors) {
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
