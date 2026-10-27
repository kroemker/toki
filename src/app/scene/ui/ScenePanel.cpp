#include "ScenePanel.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/DatabaseEdits.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/ParamReading.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/CollisionDrawing.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/NodeBounds.h"
#include "app/scene/domain/PathUsers.h"
#include "app/scene/domain/PlacementTransform.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneFlags.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneView.h"
#include "app/scene/domain/WaterBoxShape.h"
#include "app/scene/ui/PathForm.h"
#include "app/scene/ui/SceneOutliner.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr float DimmedLinkAlpha = 0.2F;
constexpr glm::vec3 LinkLift{0.0F, 30.0F, 0.0F};
constexpr ImVec4 PathColor{0.851F, 0.424F, 0.878F, 0.7F};
constexpr ImVec4 SelectedPathColor{1.0F, 0.75F, 1.0F, 1.0F};
constexpr float FollowerAlpha = 0.45F;

[[nodiscard]] std::optional<usize> findSelectedPath(const SceneSelection& selection) {
    const SceneNode* active = selection.findActive();
    if (active == nullptr || (!std::holds_alternative<PathRef>(*active) &&
                              !std::holds_alternative<PathPointRef>(*active))) {
        return std::nullopt;
    }
    return findPathOf(*active).path;
}

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
      placementEditor(workspaceService, collectionCatalog,
                      [this](const bool hasNewActors) {
                          showScene();
                          if (hasNewActors && readActors && scene) {
                              readActors(*scene);
                          }
                      }),
      meshEditor(
          viewportPanel,
          [this](SceneEditor::ModelEdit edit) { placementEditor.pickSceneEdit(std::move(edit)); },
          [this](const std::optional<MeshEditor::Preview>& preview) { showMeshPreview(preview); }),
      sceneOutliner(SceneOutliner::Hooks{
          .drawPlacement = [this](const PlacementRow& row) { drawActor(row); },
          .drawRoomMenu = [this](const usize room,
                                 const RoomHeader& header) { drawRoomMenu(room, header); },
          .findName = [this](const s16 actorId) { return findOutlinerName(actorId); },
          .findSelected = [this] { return placementEditor.findSelected(); },
          .isSelected = [this](const SceneNode& node) { return placementEditor.isSelected(node); },
          .pickRoom =
              [this](const usize room) {
                  const SceneNode node = RoomRef{.layer = sceneView.layer, .room = room};
                  static_cast<void>(pickInTree(std::span(&node, 1)));
              },
          .pickScene =
              [this] {
                  const SceneNode node = SceneRef{.layer = sceneView.layer};
                  static_cast<void>(pickInTree(std::span(&node, 1)));
              },
          .showScene = [this] { showScene(); },
          .pickNode = [this](const SceneNode& node) { pickListNode(node); },
          .pickNodes = [this](const std::span<const SceneNode> nodes) { pickInTree(nodes); },
          .drawNodeMenu = [this](const SceneNode& node) { drawNodeMenu(node); },
          .drawGroupMenu = [this](const OutlinerGroup group) { drawGroupMenu(group); },
          .findMaterials = [this] { return meshEditor.findMaterials(); },
          .pickMaterial = [this](const usize material) { meshEditor.pickMaterial(material); },
          .drawMaterialMenu =
              [this](const usize material) { meshEditor.drawMaterialMenu(material); },
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
    if (isNew) {
        meshEditor.reset();
    }

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
    previewPaths.reset();
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
    if (const Collision* collision = findLayerCollision(*scene, sceneView.layer)) {
        choices.showsCollision = sceneView.showsCollision;
        choices.showsWaterBoxes = sceneView.showsWaterBoxes && !collision->waterBoxes.empty();
        for (usize i = 0; i < CollisionColoringCount; ++i) {
            choices.collisionColorings.emplace_back(
                getCollisionColoringLabel(static_cast<CollisionColoring>(i)));
        }
        choices.collisionColoring = static_cast<usize>(sceneView.collisionColoring);
    }
    choices.editTargets.emplace_back("Mesh");
    if (findLayerCollision(*scene, sceneView.layer) != nullptr) {
        choices.editTargets.emplace_back("Collision");
        choices.isLinked = meshEditor.isLinked();
        choices.linkTooltip =
            "Link collision with the mesh, so that the positions of matching mesh and collision "
            "vertices stay synced: moving a vertex of either moves every vertex of the other "
            "that stands where it stood.";
    } else {
        meshEditor.setTarget(EditTarget::Mesh);
    }
    choices.editTarget = static_cast<usize>(meshEditor.getTarget());
    viewportPanel.setChoices(std::move(choices), [this](const viewport::ViewportChoices& chosen) {
        sceneView.lightSetting = chosen.lightSetting;
        const viewport::ActorDisplay actors =
            chosen.actors.value_or(viewport::ActorDisplay::Models);
        sceneView.showsActors = actors != viewport::ActorDisplay::Hidden;
        if (sceneView.showsActors) {
            sceneView.drawsModels = actors == viewport::ActorDisplay::Models;
        }
        if (chosen.collisionColoring < CollisionColoringCount) {
            sceneView.collisionColoring = static_cast<CollisionColoring>(chosen.collisionColoring);
        }
        meshEditor.setTarget(static_cast<EditTarget>(chosen.editTarget));
        meshEditor.setLinked(chosen.isLinked.value_or(meshEditor.isLinked()));
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
    placementEditor.getPathForm().drawRemovalQuestion(*scene);
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
    const SceneLayer& layer = scene->layers[sceneView.layer];
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    sceneOutliner.draw(*scene, sceneView, workspace != nullptr ? &workspace->getTables() : nullptr);
    const std::vector<SceneFlagUse> flags = listSceneFlags(layer, scene->actors);
    drawFlags(flags);
    drawOverlay(flags, layer);
    if (viewportPanel.getSelectionMode() == viewport::SelectionMode::Node) {
        offerPlacements(layer);
    }
    offerWaterBoxHandles();
}

void ScenePanel::showPreview(const std::vector<SceneNode>& nodes,
                             const std::optional<viewport::TransformEdit>& edit) {
    if (!scene || sceneView.layer >= scene->layers.size()) {
        return;
    }
    if (!edit) {
        isPreviewEnding = true;
        return;
    }
    SceneLayer layer = scene->layers[sceneView.layer];
    const Collision* found = findLayerCollision(*scene, sceneView.layer);
    std::optional<Collision> collision;
    if (found != nullptr) {
        collision = *found;
    }
    for (usize i = 0; i < nodes.size() && i < edit->points.size(); ++i) {
        if (const auto* box = std::get_if<WaterBoxRef>(&nodes[i])) {
            if (collision && box->layer == sceneView.layer &&
                box->box < collision->waterBoxes.size()) {
                collision->waterBoxes[box->box] =
                    makeMovedWaterBox(collision->waterBoxes[box->box], edit->points[i]);
            }
            continue;
        }
        if (const auto* point = std::get_if<PathPointRef>(&nodes[i])) {
            if (point->layer == sceneView.layer && point->path < layer.header.paths.size() &&
                point->point < layer.header.paths[point->path].points.size()) {
                layer.header.paths[point->path].points[point->point] =
                    makePosition(edit->points[i]);
            }
            continue;
        }
        const auto& where = std::get<PlacementRef>(nodes[i]);
        if (const std::optional<Placement> placement = findPlacement(*scene, where);
            placement && where.layer == sceneView.layer) {
            static_cast<void>(setLayerPlacement(
                layer, where, makeTransformedPlacement(*placement, edit->points[i], edit->result)));
        }
    }
    showLayer(layer, sceneView, collision ? &*collision : nullptr);
}

void ScenePanel::showMeshPreview(const std::optional<MeshEditor::Preview>& preview) {
    if (!preview) {
        isPreviewEnding = true;
        return;
    }
    if (!scene || sceneView.layer >= scene->layers.size()) {
        return;
    }
    viewport::ViewportContent content =
        makeSceneContent(*scene, sceneView, markerShape, nullptr,
                         preview->collision ? &*preview->collision : nullptr);
    const std::vector<std::shared_ptr<const z64::MemoryImage>> written =
        makeWrittenImages(*scene, preview->writes);
    for (usize i = 0; i < written.size() && i < content.images.size(); ++i) {
        content.images[i] = written[i];
    }
    viewportPanel.setNextFrame(std::move(content));
}

void ScenePanel::showWaterBox(const usize index, const WaterBox& box) {
    const Collision* found = findLayerCollision(*scene, sceneView.layer);
    if (found == nullptr || index >= found->waterBoxes.size()) {
        return;
    }
    Collision collision = *found;
    collision.waterBoxes[index] = box;
    showLayer(scene->layers[sceneView.layer], sceneView, &collision);
}

void ScenePanel::offerWaterBoxHandles() {
    const SceneNode* active = placementEditor.getSelection().findActive();
    const auto* where = active != nullptr ? std::get_if<WaterBoxRef>(active) : nullptr;
    const Collision* collision =
        where != nullptr ? findLayerCollision(*scene, where->layer) : nullptr;
    if (!sceneView.showsWaterBoxes || collision == nullptr || where->layer != sceneView.layer ||
        where->box >= collision->waterBoxes.size()) {
        viewportPanel.setHandles({}, nullptr);
        return;
    }
    constexpr std::array<const char*, 4> SideNames{"lowest X", "highest X", "lowest Z",
                                                   "highest Z"};
    const WaterBox& box = collision->waterBoxes[where->box];
    std::vector<viewport::ViewportHandle> handles;
    for (usize i = 0; i < WaterBoxSides.size(); ++i) {
        handles.push_back(viewport::ViewportHandle{
            .position = findWaterBoxSidePoint(box, WaterBoxSides[i]),
            .direction = getWaterBoxSideDirection(WaterBoxSides[i]),
            .tooltip = std::format("Drag to move the waterbox's {} side alone.", SideNames[i])});
    }
    viewportPanel.setHandles(
        std::move(handles),
        [this, where = *where, box](const viewport::HandleDrag& drag) {
            placementEditor.getWaterBoxForm().pickBox(
                *scene, where, makeResizedWaterBox(box, WaterBoxSides[drag.handle], drag.distance));
        },
        [this, index = where->box, box](const std::optional<viewport::HandleDrag>& drag) {
            if (!drag) {
                isPreviewEnding = true;
                return;
            }
            showWaterBox(index,
                         makeResizedWaterBox(box, WaterBoxSides[drag->handle], drag->distance));
        });
}

void ScenePanel::showEdited(const PlacementEditor::Preview& preview) {
    SceneLayer layer = scene->layers[sceneView.layer];
    SceneView view = sceneView;
    if (preview.placement && preview.placement->where.layer == sceneView.layer) {
        static_cast<void>(
            setLayerPlacement(layer, preview.placement->where, preview.placement->placement));
    }
    if (const std::optional<LightSettingForm::Draft>& light = preview.lightSetting;
        light && light->where.layer == sceneView.layer &&
        light->where.index < layer.header.lightSettings.size()) {
        layer.header.lightSettings[light->where.index] = light->setting;
        view.lightSetting = light->where.index;
    }
    if (const std::optional<PathForm::Draft>& path = preview.path;
        path && path->where.layer == sceneView.layer &&
        path->where.path < layer.header.paths.size()) {
        layer.header.paths[path->where.path] = path->path;
    }
    showLayer(layer, view);
}

void ScenePanel::showLayer(const SceneLayer& layer, const SceneView& view,
                           const Collision* collision) {
    previewPaths = layer.header.paths;
    viewportPanel.setNextFrame(makeSceneContent(*scene, view, markerShape, &layer, collision));
}

void ScenePanel::update() {
    if (!scene) {
        return;
    }
    placementEditor.applyPicked(*scene);
    if (const std::optional<u16> tinted =
            findTintedSurfaceType(*scene, sceneView, placementEditor.getSelection().findActive());
        tinted != sceneView.tintedSurfaceType) {
        sceneView.tintedSurfaceType = tinted;
        showScene();
    }
    if (isPreviewEnding) {
        isPreviewEnding = false;
        showScene();
    }
    PlacementEditor::Preview changed = placementEditor.findPreview(*scene);
    if (changed != shownPreview) {
        shownPreview = std::move(changed);
        if (!shownPreview.isEmpty() && sceneView.layer < scene->layers.size()) {
            showEdited(shownPreview);
        } else {
            showScene();
        }
    }
}

void ScenePanel::drawProperties() {
    if (!scene || scene->layers.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "Open a scene in the Assets panel.");
        return;
    }
    if (viewportPanel.getSelectionMode() != viewport::SelectionMode::Node &&
        meshEditor.hasPicked()) {
        meshEditor.drawProperties();
        return;
    }
    placementEditor.drawInspector(*scene);
}

void ScenePanel::offerPlacements(const SceneLayer& layer) {
    std::vector<glm::vec3> points;
    std::vector<SceneNode> placements;
    const auto offer = [&points, &placements](const SceneNode& where,
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
                offer(PlacementRef{.kind = PlacementKind::Actor,
                                   .layer = sceneView.layer,
                                   .room = room,
                                   .index = i},
                      layer.rooms[room].actors[i].position);
            }
        }
        for (usize i = 0; i < layer.header.transitionActors.size(); ++i) {
            offer(PlacementRef{.kind = PlacementKind::TransitionActor,
                               .layer = sceneView.layer,
                               .index = i},
                  layer.header.transitionActors[i].position);
        }
        for (usize i = 0; i < layer.header.playerEntries.size(); ++i) {
            offer(PlacementRef{.kind = PlacementKind::PlayerEntry,
                               .layer = sceneView.layer,
                               .index = i},
                  layer.header.playerEntries[i].position);
        }
    }
    if (sceneView.showsPaths) {
        for (usize path = 0; path < layer.header.paths.size(); ++path) {
            const std::vector<std::array<s16, 3>>& pathPoints = layer.header.paths[path].points;
            for (usize point = 0; point < pathPoints.size(); ++point) {
                offer(PathPointRef{.layer = sceneView.layer, .path = path, .point = point},
                      pathPoints[point]);
            }
        }
    }
    viewportPanel.setPickables(
        {.points = std::move(points)},
        [this, placements = std::move(placements)](const viewport::ViewportPanel::ViewportPick& hit,
                                                   const viewport::PickMode mode) {
            std::vector<SceneNode> nodes;
            for (const usize index : hit.points) {
                nodes.push_back(placements[index]);
            }
            if (hit.owner) {
                if (const std::optional<SceneNode> node = findNode(*hit.owner)) {
                    nodes.push_back(*node);
                }
            }
            placementEditor.pick(nodes, mode);
        });
    std::vector<usize> selectedOwners;
    for (const SceneNode& node : placementEditor.getSelection().getNodes()) {
        selectedOwners.push_back(makeNodeKey(node));
    }
    viewportPanel.setSelectedOwners(std::move(selectedOwners));
    std::optional<PlacementEditor::SelectionTransform> transform =
        placementEditor.makeTransformTarget(*scene);
    if (!transform) {
        viewportPanel.setTransformTarget(std::nullopt, nullptr);
        return;
    }
    viewportPanel.setTransformTarget(
        std::move(transform->target),
        [this, moved = transform->nodes](const viewport::TransformEdit& edit) {
            placementEditor.applyTransform(moved, edit);
        },
        [this, moved = transform->nodes](const std::optional<viewport::TransformEdit>& edit) {
            showPreview(moved, edit);
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
    if (!core::beginProperties("##pickers")) {
        return;
    }
    core::drawPropertyLabel("Layer", "The scene setup shown and edited: the child or adult, day "
                                     "or night header, or a cutscene's.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##layer", makeLayerName(current.index).c_str())) {
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
    core::endProperties();

    const auto drawn = std::ranges::count_if(
        shown.actors.models, [](const auto& entry) { return entry.second.hasDrawing(); });
    ImGui::TextColored(core::palette::TextMuted, "%d of %d kinds of actor drawn as models",
                       static_cast<int>(drawn), static_cast<int>(shown.actors.models.size()));
}

void ScenePanel::drawRoomMenu(const usize room, const RoomHeader& header) {
    if (!scene || !ImGui::BeginPopupContextItem()) {
        return;
    }
    placementEditor.drawRoomMenu(sceneView.layer, room, findCameraTarget());
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

std::array<s16, 3> ScenePanel::findCameraTarget() const {
    const core::CameraPose camera = viewportPanel.getCameraPose();
    return {static_cast<s16>(camera.target[0]), static_cast<s16>(camera.target[1]),
            static_cast<s16>(camera.target[2])};
}

void ScenePanel::pickListNode(const SceneNode& node) {
    if (!pickInTree(std::span(&node, 1)) || !scene) {
        return;
    }
    if (const auto* light = std::get_if<LightSettingRef>(&node)) {
        if (light->index != sceneView.lightSetting) {
            sceneView.lightSetting = light->index;
            showScene();
        }
    }
}

void ScenePanel::drawNodeMenu(const SceneNode& node) {
    if (!scene) {
        return;
    }
    if (const auto* light = std::get_if<LightSettingRef>(&node)) {
        placementEditor.getLightSettingForm().drawMenu(*scene, *light);
    } else if (std::holds_alternative<PathRef>(node) ||
               std::holds_alternative<PathPointRef>(node)) {
        placementEditor.getPathForm().drawMenu(*scene, node);
    } else if (const auto* box = std::get_if<WaterBoxRef>(&node)) {
        placementEditor.getWaterBoxForm().drawMenu(*scene, *box);
    }
}

void ScenePanel::drawGroupMenu(const OutlinerGroup group) {
    if (!scene) {
        return;
    }
    switch (group) {
    case OutlinerGroup::LightSettings:
        placementEditor.getLightSettingForm().drawGroupMenu(*scene, sceneView.layer);
        break;
    case OutlinerGroup::Paths:
        placementEditor.getPathForm().drawGroupMenu(*scene, sceneView.layer, findCameraTarget());
        break;
    case OutlinerGroup::SurfaceTypes:
        placementEditor.getCollisionForm().drawSurfaceTypesMenu(*scene, sceneView.layer);
        break;
    case OutlinerGroup::WaterBoxes:
        placementEditor.getWaterBoxForm().drawGroupMenu(*scene, sceneView.layer,
                                                        findCameraTarget());
        break;
    }
}

bool ScenePanel::pickInTree(const std::span<const SceneNode> nodes) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        placementEditor.pick(nodes, viewport::PickMode::Toggle);
        return false;
    }
    if (io.KeyShift) {
        placementEditor.pick(nodes, viewport::PickMode::Add);
        return false;
    }
    placementEditor.pick(nodes, viewport::PickMode::Replace);
    focusNodes(nodes);
    return true;
}

void ScenePanel::focusNodes(const std::span<const SceneNode> nodes) {
    constexpr f32 CloseUpRadius = 150.0F;
    if (!scene) {
        return;
    }
    const std::optional<NodeBounds> bounds = findNodeBounds(*scene, nodes);
    if (!bounds) {
        return;
    }
    const glm::vec3 center = (bounds->min + bounds->max) * 0.5F;
    const f32 radius = glm::length(bounds->max - bounds->min) * 0.5F;
    if (radius < 1.0F) {
        viewportPanel.focus(center);
    } else {
        viewportPanel.frameView(center, std::max(radius, CloseUpRadius));
    }
}

void ScenePanel::setCollisionNames(CollisionNames names) {
    collisionNames = std::move(names);
    sceneOutliner.setCollisionNames(&collisionNames);
    placementEditor.setCollisionNames(&collisionNames);
}

void ScenePanel::drawActor(const PlacementRow& row) {
    const PlacementRef& where = row.where;
    const s16 actorId = row.actorId;
    const actor::PlacementValues& placement = row.values;
    const auto model = scene->actors.models.find(actorId);
    const std::optional<ActorDescription> description =
        model != scene->actors.models.end()
            ? std::optional(describeActor(model->second, placement, row.loadedObjects,
                                          [this, &model](const std::string_view name) {
                                              return collectionCatalog.findCollection(
                                                  model->second.entry->actor, name);
                                          }))
            : std::nullopt;
    const bool isDrawn = description && description->isDrawn;
    std::string name = findActorName(actorId);
    if (row.label) {
        name = *row.label;
    } else if (description && !description->name.empty()) {
        name = description->name;
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (placementEditor.isSelected(where)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::TreeNodeEx("##placement", flags, "%s", "");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        const SceneNode node = where;
        static_cast<void>(pickInTree(std::span(&node, 1)));
        if (where.kind == PlacementKind::PlayerEntry && where.index != sceneView.shownEntrance) {
            sceneView.shownEntrance = where.index;
            showScene();
        }
    }
    if (description && ImGui::BeginItemTooltip()) {
        drawActorTooltip(actorId, row.position, *description);
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem()) {
        if (description) {
            drawActorMenu(model->second, placement, *description);
        }
        placementEditor.drawPlacementMenu(*scene, where);
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(isDrawn ? core::palette::Success : core::palette::TextDisabled, "%s",
                       isDrawn ? core::icons::Cube : core::icons::Diamond);
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
}

std::string ScenePanel::findOutlinerName(const s16 actorId) const {
    std::string name = findActorName(actorId);
    if (const auto model = scene->actors.models.find(actorId);
        model != scene->actors.models.end()) {
        name += " " + model->second.enumName;
        if (model->second.entry) {
            name += " " + model->second.entry->name;
        }
    }
    return name;
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

void ScenePanel::drawOverlay(const std::vector<SceneFlagUse>& flags, const SceneLayer& layer) {
    viewport::ViewportOverlay overlay;
    if (sceneView.showsActors) {
        addFlagLinks(overlay, flags);
    }
    if (sceneView.showsPaths) {
        const std::vector<ScenePath>& paths = previewPaths ? *previewPaths : layer.header.paths;
        addPathLines(overlay, paths);
        if (sceneView.showsActors) {
            addFollowerLines(overlay, paths);
        }
    }
    if (viewportPanel.getSelectionMode() != viewport::SelectionMode::Node) {
        meshEditor.offer(*scene, sceneView, overlay);
    }
    viewportPanel.setOverlay(std::move(overlay));
}

void ScenePanel::addPathLines(viewport::ViewportOverlay& overlay,
                              const std::vector<ScenePath>& paths) {
    const std::optional<usize> selected = findSelectedPath(placementEditor.getSelection());
    for (usize path = 0; path < paths.size(); ++path) {
        const u32 color = ImGui::GetColorU32(path == selected ? SelectedPathColor : PathColor);
        const std::vector<std::array<s16, 3>>& points = paths[path].points;
        for (usize i = 1; i < points.size(); ++i) {
            overlay.lines.push_back(viewport::OverlayLine{.from = makePoint(points[i - 1]),
                                                          .to = makePoint(points[i]),
                                                          .color = color,
                                                          .hasArrowAtTo = true});
        }
    }
}

void ScenePanel::addFollowerLines(viewport::ViewportOverlay& overlay,
                                  const std::vector<ScenePath>& paths) {
    const std::optional<usize> selected = findSelectedPath(placementEditor.getSelection());
    for (const PathUser& user : listPathUsers(*scene, sceneView.layer)) {
        const bool isHidden = user.where.kind == PlacementKind::Actor &&
                              user.where.room < sceneView.hiddenRooms.size() &&
                              sceneView.hiddenRooms[user.where.room];
        if (isHidden || user.path >= paths.size() || paths[user.path].points.empty()) {
            continue;
        }
        ImVec4 color = user.path == selected ? SelectedPathColor : PathColor;
        color.w = FollowerAlpha;
        overlay.lines.push_back(
            viewport::OverlayLine{.from = makePoint(user.position) + LinkLift,
                                  .to = makePoint(paths[user.path].points.front()),
                                  .color = ImGui::GetColorU32(color)});
    }
}

void ScenePanel::addFlagLinks(viewport::ViewportOverlay& overlay,
                              const std::vector<SceneFlagUse>& flags) {
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
