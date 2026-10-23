#include "SceneEditor.h"

#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "hackeroot/Tables.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

std::optional<Placement> findPlacement(const Scene& scene, const PlacementRef& where) {
    if (where.layer >= scene.layers.size()) {
        return std::nullopt;
    }
    const SceneLayer& layer = scene.layers[where.layer];
    switch (where.kind) {
    case PlacementKind::Actor:
        if (where.room < layer.rooms.size() &&
            where.index < layer.rooms[where.room].actors.size()) {
            return layer.rooms[where.room].actors[where.index];
        }
        break;
    case PlacementKind::PlayerEntry:
        if (where.index < layer.header.playerEntries.size()) {
            return layer.header.playerEntries[where.index];
        }
        break;
    case PlacementKind::TransitionActor:
        if (where.index < layer.header.transitionActors.size()) {
            return layer.header.transitionActors[where.index];
        }
        break;
    }
    return std::nullopt;
}

std::optional<LightSetting> findLightSetting(const Scene& scene, const usize layer,
                                             const usize index) {
    if (layer >= scene.layers.size() || index >= scene.layers[layer].header.lightSettings.size()) {
        return std::nullopt;
    }
    return scene.layers[layer].header.lightSettings[index];
}

std::optional<ScenePath> findPath(const Scene& scene, const usize layer, const usize index) {
    if (layer >= scene.layers.size() || index >= scene.layers[layer].header.paths.size()) {
        return std::nullopt;
    }
    return scene.layers[layer].header.paths[index];
}

bool hasUnreadActors(const Scene& scene) {
    const auto isUnread = [&scene](const s16 id) { return !scene.actors.models.contains(id); };
    for (const SceneLayer& layer : scene.layers) {
        for (const RoomHeader& room : layer.rooms) {
            if (std::ranges::any_of(room.actors, isUnread, &ActorPlacement::id)) {
                return true;
            }
        }
        if (std::ranges::any_of(layer.header.transitionActors, isUnread,
                                &TransitionActorPlacement::id)) {
            return true;
        }
    }
    return false;
}

std::optional<s16> findMissingObject(const Scene& scene, const PlacementRef& where,
                                     const std::string_view object,
                                     const hackeroot::Tables& tables) {
    if (where.kind != PlacementKind::Actor || where.layer >= scene.layers.size()) {
        return std::nullopt;
    }
    const std::vector<std::string> loaded =
        listLoadedObjects(scene.layers[where.layer], where.room, tables);
    if (std::ranges::contains(loaded, object)) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(tables.objects, object, &hackeroot::ObjectEntry::name);
    if (found == tables.objects.end()) {
        return std::nullopt;
    }
    return static_cast<s16>(found->id);
}

std::optional<PlacementList> findPlacementList(const Scene& scene, const PlacementRef& where) {
    if (where.layer >= scene.layers.size()) {
        return std::nullopt;
    }
    return PlacementList{
        .kind = where.kind, .layer = scene.layers[where.layer].index, .room = where.room};
}

namespace {

void showModel(Scene& scene, const SceneModel& model) {
    scene.images = model.layOut();
    scene.diagnostics.clear();
    readSceneLayers(scene);
}

} // namespace

std::expected<void, base::Diagnostic> SceneEditor::apply(Scene& scene, const PlacementRef& where,
                                                         const Edit& edit, const bool joinsLast) {
    const std::optional<PlacementList> list = findPlacementList(scene, where);
    if (!list) {
        return std::unexpected(
            base::makeError({}, std::format("the scene has no layer {}", where.layer)));
    }
    return applyToModel(
        scene, [&edit, &list](SceneModel& model) { return edit(model, *list); }, joinsLast);
}

std::expected<void, base::Diagnostic> SceneEditor::applyToModel(Scene& scene, const ModelEdit& edit,
                                                                const bool joinsLast) {
    SceneModel before = sceneModel ? *sceneModel : SceneModel(scene);
    SceneModel edited = before;
    if (std::expected<void, base::Diagnostic> done = edit(edited); !done) {
        return done;
    }
    showModel(scene, edited);
    sceneModel = std::move(edited);
    if (!joinsLast || undone.empty()) {
        undone.push_back(std::move(before));
        if (undone.size() > HistoryLength) {
            undone.erase(undone.begin());
        }
    }
    redone.clear();
    return {};
}

bool SceneEditor::undo(Scene& scene) {
    if (undone.empty() || !sceneModel) {
        return false;
    }
    redone.push_back(std::move(*sceneModel));
    sceneModel = std::move(undone.back());
    undone.pop_back();
    showModel(scene, *sceneModel);
    return true;
}

bool SceneEditor::redo(Scene& scene) {
    if (redone.empty() || !sceneModel) {
        return false;
    }
    undone.push_back(std::move(*sceneModel));
    sceneModel = std::move(redone.back());
    redone.pop_back();
    showModel(scene, *sceneModel);
    return true;
}

} // namespace toki::scene
