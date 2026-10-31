#include "PathUsers.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"

#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] const actor::ActorEntry* findEntry(const SceneActors& actors, const s16 actorId) {
    const auto model = actors.models.find(actorId);
    return model != actors.models.end() && model->second.entry ? &*model->second.entry : nullptr;
}

[[nodiscard]] actor::PlacementValues readValues(const SceneActors& actors,
                                                const Placement& placement,
                                                const std::optional<actor::Age> age) {
    if (const auto* placed = std::get_if<ActorPlacement>(&placement)) {
        return makePlacementValues(actors, static_cast<u16>(placed->params), placed->rotation, age);
    }
    const auto& transition = std::get<TransitionActorPlacement>(placement);
    return makePlacementValues(actors, static_cast<u16>(transition.params),
                               {0, transition.rotationY, 0}, age);
}

void writeValues(Placement& placement, const actor::PlacementValues& values) {
    if (auto* placed = std::get_if<ActorPlacement>(&placement)) {
        placed->params = static_cast<s16>(values.params);
        placed->rotation = values.rotation;
        return;
    }
    auto& transition = std::get<TransitionActorPlacement>(placement);
    transition.params = static_cast<s16>(values.params);
    transition.rotationY = values.rotation[1];
}

/// Every placement of `layer`, in its rooms and between them, each with where
/// it is.
[[nodiscard]] std::vector<std::pair<PlacementRef, Placement>> listPlacements(const Scene& scene,
                                                                             const usize layer) {
    std::vector<std::pair<PlacementRef, Placement>> placements;
    const SceneLayer& shown = scene.layers[layer];
    for (usize room = 0; room < shown.rooms.size(); ++room) {
        for (usize i = 0; i < shown.rooms[room].actors.size(); ++i) {
            placements.emplace_back(
                PlacementRef{
                    .kind = PlacementKind::Actor, .layer = layer, .room = room, .index = i},
                shown.rooms[room].actors[i]);
        }
    }
    for (usize i = 0; i < shown.header.transitionActors.size(); ++i) {
        placements.emplace_back(
            PlacementRef{.kind = PlacementKind::TransitionActor, .layer = layer, .index = i},
            shown.header.transitionActors[i]);
    }
    return placements;
}

[[nodiscard]] s16 getActorId(const Placement& placement) {
    return std::visit([](const auto& placed) { return placed.id; }, placement);
}

/// Whether another layer than `layer` reads `room`'s header for it, as a
/// layer reads the main header of a room with none of its own.
[[nodiscard]] bool isSharedRoom(const Scene& scene, const usize layer, const usize room) {
    const u32 address = scene.layers[layer].rooms[room].address;
    for (usize other = 0; other < scene.layers.size(); ++other) {
        if (other != layer && room < scene.layers[other].rooms.size() &&
            scene.layers[other].rooms[room].address == address) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<s64> findPathNumber(const actor::ActorParam& param,
                                  const actor::PlacementValues& placement) {
    if (!param.isPath || param.mask == 0) {
        return std::nullopt;
    }
    const s64 value = actor::readParamValue(param, placement);
    const s64 none = param.isSigned ? -1 : (s64{1} << actor::findMaskWidth(param.mask)) - 1;
    if (value == none || value < 0) {
        return std::nullopt;
    }
    return value;
}

std::vector<PathUser> listPathUsers(const Scene& scene, const usize layer) {
    std::vector<PathUser> users;
    if (layer >= scene.layers.size()) {
        return users;
    }
    const usize count = scene.layers[layer].header.paths.size();
    const std::optional<actor::Age> age = findLayerAge(scene.layers[layer].index);
    for (const auto& [where, placement] : listPlacements(scene, layer)) {
        const s16 actorId = getActorId(placement);
        const actor::ActorEntry* entry = findEntry(scene.actors, actorId);
        if (entry == nullptr) {
            continue;
        }
        const actor::PlacementValues values = readValues(scene.actors, placement, age);
        for (const actor::ActorParam& param : entry->params) {
            const std::optional<s64> path = findPathNumber(param, values);
            if (path && static_cast<usize>(*path) < count) {
                users.push_back(PathUser{
                    .where = where,
                    .actorId = actorId,
                    .position =
                        std::visit([](const auto& placed) { return placed.position; }, placement),
                    .param = param.name,
                    .path = static_cast<usize>(*path)});
            }
        }
    }
    return users;
}

std::vector<PlacementChange> listPathRemovalChanges(const Scene& scene, const usize layer,
                                                    const usize path) {
    std::vector<PlacementChange> changes;
    if (layer >= scene.layers.size()) {
        return changes;
    }
    const usize count = scene.layers[layer].header.paths.size();
    const std::optional<actor::Age> age = findLayerAge(scene.layers[layer].index);
    for (const auto& [where, placement] : listPlacements(scene, layer)) {
        const actor::ActorEntry* entry = findEntry(scene.actors, getActorId(placement));
        const std::optional<PlacementList> list = findPlacementList(scene, where);
        if (entry == nullptr || !list ||
            (where.kind == PlacementKind::Actor && isSharedRoom(scene, layer, where.room))) {
            continue;
        }
        actor::PlacementValues values = readValues(scene.actors, placement, age);
        bool isChanged = false;
        for (const actor::ActorParam& param : entry->params) {
            const std::optional<s64> followed = findPathNumber(param, values);
            if (followed && static_cast<usize>(*followed) > path &&
                static_cast<usize>(*followed) < count) {
                values = actor::writeParamValue(param, values, *followed - 1);
                isChanged = true;
            }
        }
        if (isChanged) {
            Placement changed = placement;
            writeValues(changed, values);
            changes.push_back(
                PlacementChange{.list = *list, .index = where.index, .placement = changed});
        }
    }
    return changes;
}

} // namespace toki::scene
