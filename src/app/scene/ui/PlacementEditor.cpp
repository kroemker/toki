#include "PlacementEditor.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/PathEdits.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/PlacementTransform.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <climits>
#include <cmath>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 7.0F;
/// What a new actor is until it is picked: a green rupee, at params 0, which
/// any room can hold.
constexpr std::string_view DefaultActor = "ACTOR_EN_ITEM00";

[[nodiscard]] actor::PlacementValues makeValues(const Scene& scene, const PlacementRef& where,
                                                const Placement& placement) {
    const std::optional<actor::Age> age = findLayerAge(scene.layers[where.layer].index);
    if (const auto* transition = std::get_if<TransitionActorPlacement>(&placement)) {
        return makePlacementValues(scene.actors, static_cast<u16>(transition->params),
                                   {0, transition->rotationY, 0}, age);
    }
    const auto& actor = std::get<ActorPlacement>(placement);
    return makePlacementValues(scene.actors, static_cast<u16>(actor.params), actor.rotation, age);
}

void setValues(Placement& placement, const actor::PlacementValues& values) {
    if (auto* transition = std::get_if<TransitionActorPlacement>(&placement)) {
        transition->params = static_cast<s16>(values.params);
        transition->rotationY = values.rotation[1];
        return;
    }
    auto& actor = std::get<ActorPlacement>(placement);
    actor.params = static_cast<s16>(values.params);
    actor.rotation = values.rotation;
}

[[nodiscard]] std::array<bool, 3> findParamAxes(const actor::ActorEntry* entry) {
    std::array<bool, 3> axes{};
    if (entry == nullptr) {
        return axes;
    }
    for (const actor::ActorParam& param : entry->params) {
        if (param.source != actor::ParamSource::Params) {
            axes[static_cast<usize>(param.source) - 1] = true;
        }
    }
    return axes;
}

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

/// Whether `node` is still something `scene` holds.
[[nodiscard]] bool isInScene(const Scene& scene, const SceneNode& node) {
    if (const auto* placement = std::get_if<PlacementRef>(&node)) {
        return findPlacement(scene, *placement).has_value();
    }
    if (const auto* sceneNode = std::get_if<SceneRef>(&node)) {
        return sceneNode->layer < scene.layers.size();
    }
    if (const auto* room = std::get_if<RoomRef>(&node)) {
        return room->layer < scene.layers.size() &&
               room->room < scene.layers[room->layer].rooms.size();
    }
    if (const auto* light = std::get_if<LightSettingRef>(&node)) {
        return findLightSetting(scene, light->layer, light->index).has_value();
    }
    if (const auto* path = std::get_if<PathRef>(&node)) {
        return findPath(scene, path->layer, path->path).has_value();
    }
    if (const auto* polygon = std::get_if<CollisionPolygonRef>(&node)) {
        const Collision* collision = findLayerCollision(scene, polygon->layer);
        return collision != nullptr && polygon->polygon < collision->polygons.size();
    }
    if (const auto* box = std::get_if<WaterBoxRef>(&node)) {
        const Collision* collision = findLayerCollision(scene, box->layer);
        return collision != nullptr && box->box < collision->waterBoxes.size();
    }
    const auto& point = std::get<PathPointRef>(node);
    const std::optional<ScenePath> path = findPath(scene, point.layer, point.path);
    return path && point.point < path->points.size();
}

[[nodiscard]] std::string makeWhere(const PlacementRef& where) {
    switch (where.kind) {
    case PlacementKind::Actor:
        return std::format("Room {} · #{}", where.room, where.index);
    case PlacementKind::TransitionActor:
        return std::format("Transition actor #{}", where.index);
    case PlacementKind::PlayerEntry:
        return std::format("Player entry #{}", where.index);
    }
    return {};
}

} // namespace

s16 PlacementEditor::findDefaultActor() const {
    if (const hackeroot::Workspace* workspace = workspaceService.getWorkspace()) {
        for (const hackeroot::ActorEntry& actor : workspace->getTables().actors) {
            if (actor.enumName == DefaultActor) {
                return static_cast<s16>(actor.id);
            }
        }
    }
    return 1;
}

PlacementEditor::PlacementEditor(const workspace::WorkspaceService& workspaceService,
                                 actor::CollectionCatalog& collectionCatalog, Show show)
    : workspaceService(workspaceService), collectionCatalog(collectionCatalog),
      show(std::move(show)), actorForm(workspaceService),
      headerForm(workspaceService,
                 [this](Scene& scene, const SceneEditor::ModelEdit& edit) {
                     return applyToModel(scene, edit);
                 }),
      lightSettingForm([this](SceneEditor::ModelEdit edit, std::optional<SceneNode> select) {
          pickModelEdit(std::move(edit), std::move(select));
      }),
      pathForm([this](SceneEditor::ModelEdit edit, std::optional<SceneNode> select) {
          pickModelEdit(std::move(edit), std::move(select));
      }) {}

void PlacementEditor::refresh(const Scene& scene) {
    resetDrafts();
    selection.removeIf([&scene](const SceneNode& node) { return !isInScene(scene, node); });
}

void PlacementEditor::resetDrafts() {
    draft.reset();
    lightSettingForm.reset();
    pathForm.reset();
}

void PlacementEditor::pickModelEdit(SceneEditor::ModelEdit edit, std::optional<SceneNode> select) {
    picked = [this, edit = std::move(edit), select = std::move(select)](Scene& edited) {
        if (applyToModel(edited, edit) && select) {
            selection.select(*select);
        }
    };
}

bool PlacementEditor::apply(Scene& scene, const PlacementRef& where, const SceneEditor::Edit& edit,
                            const bool joinsLast) {
    return showApplied(scene, sceneEditor.apply(scene, where, edit, joinsLast));
}

bool PlacementEditor::applyToModel(Scene& scene, const SceneEditor::ModelEdit& edit) {
    return showApplied(scene, sceneEditor.applyToModel(scene, edit));
}

bool PlacementEditor::showApplied(Scene& scene,
                                  const std::expected<void, base::Diagnostic>& applied) {
    resetDrafts();
    if (!applied) {
        failure = applied.error().message;
        return false;
    }
    failure.clear();
    showChanged(scene);
    return true;
}

void PlacementEditor::showChanged(Scene& scene) {
    refresh(scene);
    show(hasUnreadActors(scene));
}

void PlacementEditor::undo(Scene& scene) {
    if (sceneEditor.undo(scene)) {
        failure.clear();
        showChanged(scene);
    }
}

void PlacementEditor::redo(Scene& scene) {
    if (sceneEditor.redo(scene)) {
        failure.clear();
        showChanged(scene);
    }
}

void PlacementEditor::addObjectFor(Scene& scene, const PlacementRef& where, const s16 actorId) {
    if (findActorObject) {
        if (const std::optional<std::string> object = findActorObject(actorId)) {
            addObject(scene, where, *object, true);
        }
    }
}

void PlacementEditor::addObject(Scene& scene, const PlacementRef& where, const std::string& object,
                                const bool joinsLast) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (where.kind != PlacementKind::Actor || workspace == nullptr) {
        return;
    }
    const std::optional<s16> missing =
        findMissingObject(scene, where, object, workspace->getTables());
    if (!missing) {
        return;
    }
    const bool isAdded = apply(
        scene, where,
        [missing](SceneModel& model, const PlacementList& list) {
            return addRoomObjects(model, list.layer, list.room, std::span(&*missing, 1));
        },
        joinsLast);
    if (!isAdded) {
        failure = std::format("{} is not in the room's object list, and was not added: {}", object,
                              failure);
    }
}

void PlacementEditor::drawInspector(Scene& scene) {
    const SceneNode* node = selection.findActive();
    if (const auto* room = node != nullptr ? std::get_if<RoomRef>(node) : nullptr) {
        headerForm.drawRoom(scene, *room, selection.getNodes().size());
        return;
    }
    if (const auto* sceneNode = node != nullptr ? std::get_if<SceneRef>(node) : nullptr) {
        headerForm.drawScene(scene, *sceneNode, selection.getNodes().size());
        return;
    }
    if (const auto* light = node != nullptr ? std::get_if<LightSettingRef>(node) : nullptr) {
        lightSettingForm.draw(scene, *light, selection.getNodes().size());
        return;
    }
    if (node != nullptr &&
        (std::holds_alternative<PathRef>(*node) || std::holds_alternative<PathPointRef>(*node))) {
        pathForm.draw(scene, *node, selection.getNodes().size());
        return;
    }
    if (const auto* polygon = node != nullptr ? std::get_if<CollisionPolygonRef>(node) : nullptr) {
        collisionForm.drawPolygon(scene, *polygon, selection.getNodes().size());
        return;
    }
    if (const auto* box = node != nullptr ? std::get_if<WaterBoxRef>(node) : nullptr) {
        collisionForm.drawWaterBox(scene, *box, selection.getNodes().size());
        return;
    }
    const std::optional<PlacementRef> active = findSelected();
    if (!active) {
        ImGui::TextColored(
            core::palette::TextMuted,
            "Select the scene, a room, a placement, a light setting, a path or a piece of "
            "collision in the Scene panel or the viewport.");
        return;
    }
    if (!draft) {
        draft = findPlacement(scene, *active);
        if (!draft) {
            return;
        }
    }
    const PlacementRef where = *active;
    const s16 actorId = std::visit([](const auto& held) { return held.id; }, *draft);
    const auto model = scene.actors.models.find(actorId);
    const actor::ActorEntry* entry =
        model != scene.actors.models.end() && model->second.entry ? &*model->second.entry : nullptr;
    drawHeader(where, actorId, entry);
    bool isDone = false;
    if (ImGui::CollapsingHeader("Actor", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##actor", LabelWidth)) {
        const std::array<bool, 3> axes = findParamAxes(entry);
        if (auto* actor = std::get_if<ActorPlacement>(&*draft)) {
            isDone = actorForm.drawActor(*actor, axes);
        } else if (auto* transition = std::get_if<TransitionActorPlacement>(&*draft)) {
            isDone = actorForm.drawTransitionActor(*transition, axes[1]);
        }
        core::endProperties();
    }
    isDone = drawParams(scene, where, entry, actorId) || isDone;
    if (entry != nullptr) {
        drawObject(scene, where, *entry);
    }
    drawActions(where);
    if (isDone) {
        commitDraft(scene, where);
    }
}

void PlacementEditor::drawHeader(const PlacementRef& where, const s16 actorId,
                                 const actor::ActorEntry* entry) const {
    std::string enumName = std::format("0x{:04X}", static_cast<u16>(actorId));
    if (const hackeroot::Workspace* workspace = workspaceService.getWorkspace()) {
        const std::vector<hackeroot::ActorEntry>& actors = workspace->getTables().actors;
        const auto index = static_cast<usize>(static_cast<u16>(actorId));
        if (index < actors.size() && !actors[index].enumName.empty()) {
            enumName = actors[index].enumName;
        }
    }
    const std::string name = entry != nullptr && !entry->name.empty() ? entry->name : enumName;
    ImGui::TextColored(core::palette::Accent, "%s", core::icons::Cube);
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
    const std::string details =
        name == enumName ? makeWhere(where) : std::format("{} · {}", enumName, makeWhere(where));
    ImGui::TextColored(core::palette::TextMuted, "%s", details.c_str());
    if (const usize count = selection.getNodes().size(); count > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           count);
    }
    ImGui::Spacing();
}

bool PlacementEditor::drawParams(const Scene& scene, const PlacementRef& where,
                                 const actor::ActorEntry* entry, const s16 actorId) {
    if (!ImGui::CollapsingHeader("Params", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }
    if (entry == nullptr || entry->params.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "%s",
                           entry == nullptr ? "The actor database has no entry for this actor."
                                            : "Its entry in the actor database names no params.");
        ImGui::TextColored(core::palette::TextMuted, "The Actor section's Params field sets them.");
        if (const auto model = scene.actors.models.find(actorId);
            openActorEditor && model != scene.actors.models.end() &&
            ImGui::Button("Edit in the Actor Database")) {
            openActorEditor(model->second.enumName);
        }
        return false;
    }
    if (!core::beginProperties("##params", LabelWidth)) {
        return false;
    }
    actor::PlacementValues values = makeValues(scene, where, *draft);
    const bool isDone =
        paramsForm.draw(*entry, values, [this, &entry](const std::string_view name) {
            return collectionCatalog.findCollection(entry->actor, name);
        });
    setValues(*draft, values);
    if (!entry->variants.empty()) {
        core::drawPropertyLabel("Variant", "The first variant of the actor's entry whose match "
                                           "these params hold for, which is what is drawn.");
        const actor::ActorVariant* variant = actor::findVariant(*entry, values);
        const std::string text =
            variant != nullptr ? std::format("{} of {}", variant - entry->variants.data() + 1,
                                             entry->variants.size())
                               : std::format("None of {} matches: marked", entry->variants.size());
        ImGui::TextColored(variant != nullptr ? core::palette::Text : core::palette::Warning, "%s",
                           text.c_str());
    }
    core::endProperties();
    if (openActorEditor && ImGui::SmallButton("Edit in the Actor Database")) {
        openActorEditor(entry->actor);
    }
    return isDone;
}

void PlacementEditor::drawObject(const Scene& scene, const PlacementRef& where,
                                 const actor::ActorEntry& entry) {
    const actor::ActorVariant* variant =
        actor::findVariant(entry, makeValues(scene, where, *draft));
    const std::optional<std::string> object =
        variant != nullptr && variant->object ? variant->object : entry.object;
    if (!object || !ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##object", LabelWidth)) {
        return;
    }
    core::drawPropertyLabel("Needs", "The object the actor database says the actor is drawn "
                                     "from, which the game must have loaded to spawn it.");
    ImGui::TextUnformatted(object->c_str());
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (where.kind == PlacementKind::Actor && workspace != nullptr) {
        core::drawPropertyLabel("Loaded", "Whether a keep object or the room's object list in "
                                          "this layer loads it.");
        if (!findMissingObject(scene, where, *object, workspace->getTables())) {
            ImGui::TextColored(core::palette::Success, "%s Yes", core::icons::Check);
        } else {
            ImGui::TextColored(core::palette::Warning, "%s No", core::icons::Warning);
            ImGui::SameLine();
            if (ImGui::SmallButton("Add to the Object List")) {
                picked = [this, where, name = *object](Scene& edited) {
                    addObject(edited, where, name);
                };
            }
        }
    }
    core::endProperties();
}

void PlacementEditor::drawActions(const PlacementRef& where) {
    ImGui::Spacing();
    ImGui::BeginDisabled(where.kind == PlacementKind::PlayerEntry);
    const usize count = listActedOn(where).size();
    const std::string suffix = count > 1 ? std::format(" {}", count) : std::string();
    if (ImGui::Button(std::format("Duplicate{}", suffix).c_str())) {
        pickDuplicate(listActedOn(where));
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Danger);
    if (ImGui::Button(std::format("Delete{}", suffix).c_str())) {
        pickDelete(listActedOn(where));
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (where.kind == PlacementKind::PlayerEntry) {
        ImGui::SetItemTooltip("The spawn list names the player entries by index.");
    }
}

void PlacementEditor::commitDraft(Scene& scene, const PlacementRef& where) {
    const Placement edited = *draft;
    const bool isSet =
        apply(scene, where, [&edited, &where](SceneModel& model, const PlacementList& list) {
            if (const auto* transition = std::get_if<TransitionActorPlacement>(&edited)) {
                return setTransitionActor(model, list.layer, where.index, *transition);
            }
            return setPlacement(model, list, where.index, std::get<ActorPlacement>(edited));
        });
    if (const auto* actor = std::get_if<ActorPlacement>(&edited); isSet && actor != nullptr) {
        addObjectFor(scene, where, actor->id);
    }
}

std::optional<PlacementEditor::Draft> PlacementEditor::findChangedDraft(const Scene& scene) const {
    const std::optional<PlacementRef> where = findSelected();
    if (!draft || !where || findPlacement(scene, *where) == draft) {
        return std::nullopt;
    }
    return Draft{.where = *where, .placement = *draft};
}

PlacementEditor::Preview PlacementEditor::findPreview(const Scene& scene) const {
    return Preview{.placement = findChangedDraft(scene),
                   .lightSetting = lightSettingForm.findChangedDraft(scene),
                   .path = pathForm.findChangedDraft(scene)};
}

std::optional<PlacementRef> PlacementEditor::findSelected() const {
    const SceneNode* active = selection.findActive();
    const auto* placement = active != nullptr ? std::get_if<PlacementRef>(active) : nullptr;
    return placement != nullptr ? std::optional(*placement) : std::nullopt;
}

std::vector<PlacementRef> PlacementEditor::listActedOn(const PlacementRef& where) const {
    std::vector<PlacementRef> placements =
        selection.contains(where) ? selection.listPlacements() : std::vector{where};
    std::erase_if(placements, [](const PlacementRef& placement) {
        return placement.kind == PlacementKind::PlayerEntry;
    });
    return placements;
}

void PlacementEditor::pickDuplicate(std::vector<PlacementRef> placements) {
    picked = [this, placements = std::move(placements)](Scene& edited) {
        std::vector<SceneNode> added;
        for (const PlacementRef& where : placements) {
            if (const std::optional<Placement> placement = findPlacement(edited, where)) {
                if (const std::optional<PlacementRef> copy =
                        add(edited, where, *placement, !added.empty())) {
                    added.emplace_back(*copy);
                }
            }
        }
        selection.pick(added, PickMode::Replace);
    };
}

void PlacementEditor::pickDelete(std::vector<PlacementRef> placements) {
    std::ranges::sort(placements, [](const PlacementRef& first, const PlacementRef& second) {
        return first.index > second.index;
    });
    picked = [this, placements = std::move(placements)](Scene& edited) {
        selection.clear();
        bool isJoined = false;
        for (const PlacementRef& where : placements) {
            isJoined = apply(
                           edited, where,
                           [where](SceneModel& model, const PlacementList& list) {
                               return removePlacement(model, list, where.index);
                           },
                           isJoined) ||
                       isJoined;
        }
    };
}

void PlacementEditor::drawPlacementMenu(const Scene& scene, const PlacementRef& where) {
    const std::optional<Placement> placement = findPlacement(scene, where);
    if (!placement) {
        return;
    }
    ImGui::Separator();
    const bool isPlayerEntry = where.kind == PlacementKind::PlayerEntry;
    if (ImGui::MenuItem("Duplicate", nullptr, false, !isPlayerEntry)) {
        pickDuplicate(listActedOn(where));
    }
    if (ImGui::MenuItem("Delete", nullptr, false, !isPlayerEntry)) {
        pickDelete(listActedOn(where));
    }
    if (isPlayerEntry) {
        ImGui::SetItemTooltip("The spawn list names the player entries by index.");
    }
}

void PlacementEditor::drawRoomMenu(const usize layer, const usize room,
                                   const std::array<s16, 3>& position) {
    ImGui::Separator();
    if (ImGui::MenuItem("Add Actor")) {
        picked = [this, layer, room,
                  placement = ActorPlacement{.id = findDefaultActor(), .position = position}](
                     Scene& edited) {
            add(edited, PlacementRef{.kind = PlacementKind::Actor, .layer = layer, .room = room},
                placement);
        };
    }
}

std::optional<PlacementEditor::SelectionTransform>
PlacementEditor::makeTransformTarget(const Scene& scene) const {
    SelectionTransform made;
    viewport::TransformTarget& target = made.target;
    target.scaleNote = "Actors set their own scale.";
    const SceneNode* active = selection.findActive();
    glm::vec3 sum(0.0F);
    for (const SceneNode& node : selection.listMovable()) {
        if (const auto* point = std::get_if<PathPointRef>(&node)) {
            const std::optional<ScenePath> path = findPath(scene, point->layer, point->path);
            if (!path || point->point >= path->points.size()) {
                continue;
            }
            if (active != nullptr && *active == node) {
                target.activePoint = target.points.size();
            }
            const std::array<s16, 3>& position = path->points[point->point];
            target.points.emplace_back(position[0], position[1], position[2]);
            sum += target.points.back();
            made.nodes.push_back(node);
            continue;
        }
        const auto& where = std::get<PlacementRef>(node);
        const std::optional<Placement> placement = findPlacement(scene, where);
        if (!placement) {
            continue;
        }
        if (active != nullptr && *active == node) {
            target.activePoint = target.points.size();
        }
        const std::array<s16, 3> position =
            std::visit([](const auto& held) { return held.position; }, *placement);
        target.points.emplace_back(position[0], position[1], position[2]);
        sum += target.points.back();
        made.nodes.push_back(node);
        if (std::holds_alternative<TransitionActorPlacement>(*placement)) {
            target.isTurnable[0] = false;
            target.isTurnable[2] = false;
            target.turnNote = "a transition actor turns only about Y";
            continue;
        }
        const s16 actorId = std::get<ActorPlacement>(*placement).id;
        const auto model = scene.actors.models.find(actorId);
        const std::array<bool, 3> axes = findParamAxes(
            model != scene.actors.models.end() && model->second.entry ? &*model->second.entry
                                                                      : nullptr);
        if (axes[1]) {
            target.isTurnable[1] = false;
        }
        if (axes[0] || axes[1] || axes[2]) {
            target.isTurnable[0] = false;
            target.isTurnable[2] = false;
            target.turnNote = "a selected actor reads params from its rotation, which a turn "
                              "about X or Z would change on every axis";
        }
    }
    if (made.nodes.empty()) {
        return std::nullopt;
    }
    target.pivot = sum / static_cast<f32>(made.nodes.size());
    return made;
}

void PlacementEditor::applyTransform(std::vector<SceneNode> nodes,
                                     const viewport::TransformEdit& edit) {
    picked = [this, nodes = std::move(nodes), edit](Scene& edited) {
        bool isJoined = false;
        for (usize i = 0; i < nodes.size() && i < edit.points.size(); ++i) {
            const auto* moved = std::get_if<PlacementRef>(&nodes[i]);
            if (moved == nullptr) {
                continue;
            }
            const PlacementRef& where = *moved;
            std::optional<Placement> placement = findPlacement(edited, where);
            if (!placement) {
                continue;
            }
            placement = makeTransformedPlacement(*placement, edit.points[i], edit.result);
            isJoined =
                apply(
                    edited, where,
                    [&placement, &where](SceneModel& model, const PlacementList& list) {
                        if (const auto* transition =
                                std::get_if<TransitionActorPlacement>(&*placement)) {
                            return setTransitionActor(model, list.layer, where.index, *transition);
                        }
                        return setPlacement(model, list, where.index,
                                            std::get<ActorPlacement>(*placement));
                    },
                    isJoined) ||
                isJoined;
        }
        static_cast<void>(applyPathMoves(edited, nodes, edit.points, isJoined));
    };
}

bool PlacementEditor::applyPathMoves(Scene& scene, const std::span<const SceneNode> nodes,
                                     const std::span<const glm::vec3> points,
                                     const bool joinsLast) {
    std::map<std::pair<usize, usize>, ScenePath> paths;
    for (usize i = 0; i < nodes.size() && i < points.size(); ++i) {
        const auto* point = std::get_if<PathPointRef>(&nodes[i]);
        if (point == nullptr) {
            continue;
        }
        const std::pair key(point->layer, point->path);
        if (!paths.contains(key)) {
            if (std::optional<ScenePath> path = findPath(scene, point->layer, point->path)) {
                paths.emplace(key, std::move(*path));
            }
        }
        const auto found = paths.find(key);
        if (found != paths.end() && point->point < found->second.points.size()) {
            found->second.points[point->point] = {roundToS16(points[i].x), roundToS16(points[i].y),
                                                  roundToS16(points[i].z)};
        }
    }
    bool isJoined = joinsLast;
    for (const auto& [key, path] : paths) {
        const usize gameLayer = scene.layers[key.first].index;
        isJoined =
            showApplied(scene, sceneEditor.applyToModel(
                                   scene,
                                   [gameLayer, index = key.second, &path](SceneModel& model) {
                                       return setPathPoints(model, gameLayer, index, path.points);
                                   },
                                   isJoined)) ||
            isJoined;
    }
    return isJoined;
}

void PlacementEditor::applyPicked(Scene& scene) {
    if (picked) {
        std::exchange(picked, nullptr)(scene);
    }
}

std::optional<PlacementRef> PlacementEditor::add(Scene& scene, const PlacementRef& where,
                                                 const Placement& placement, const bool joinsLast) {
    std::optional<usize> added;
    apply(
        scene, where,
        [&placement, &added](SceneModel& model, const PlacementList& list) {
            std::expected<usize, base::Diagnostic> index =
                list.kind == PlacementKind::TransitionActor
                    ? addTransitionActor(model, list.layer,
                                         std::get<TransitionActorPlacement>(placement))
                    : addPlacement(model, list, std::get<ActorPlacement>(placement));
            if (!index) {
                return std::expected<void, base::Diagnostic>(std::unexpected(index.error()));
            }
            added = *index;
            return std::expected<void, base::Diagnostic>();
        },
        joinsLast);
    if (!added) {
        return std::nullopt;
    }
    PlacementRef chosen = where;
    chosen.index = *added;
    select(chosen);
    if (const auto* actor = std::get_if<ActorPlacement>(&placement)) {
        addObjectFor(scene, chosen, actor->id);
    }
    return chosen;
}

} // namespace toki::scene
