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

std::expected<void, base::Diagnostic> SceneEditor::apply(Scene& scene, const PlacementRef& where,
                                                         const Edit& edit) {
    const std::optional<PlacementList> list = findPlacementList(scene, where);
    if (!list) {
        return std::unexpected(
            base::makeError({}, std::format("the scene has no layer {}", where.layer)));
    }
    SceneModel edited = sceneModel ? *sceneModel : SceneModel(scene);
    if (std::expected<void, base::Diagnostic> done = edit(edited, *list); !done) {
        return done;
    }
    scene.images = edited.layOut();
    scene.diagnostics.clear();
    readSceneLayers(scene);
    sceneModel = std::move(edited);
    return {};
}

} // namespace toki::scene
