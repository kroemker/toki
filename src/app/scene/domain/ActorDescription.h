#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// What the Scene panel says of one placed actor: what it is drawn as and where
/// in its overlay each answer was read, and what keeps it from being drawn
/// fully.
struct ActorDescription {
    bool isDrawn = false;
    std::vector<std::string> lines;
    std::vector<std::string> problems;
};

/// Describes `actor`. `loadedObjects`, when given, are the objects the game has
/// loaded where the actor is placed, the room's object list and the keep
/// objects: an actor whose profile names another one is not spawned by the game,
/// and is said so.
[[nodiscard]] ActorDescription
describeActor(const SceneActorModel& actor,
              std::optional<std::span<const std::string>> loadedObjects);

/// The objects the game has loaded while `room` of `layer` is: `gameplay_keep`,
/// the scene's keep object, and the room's object list, by name.
[[nodiscard]] std::vector<std::string> listLoadedObjects(const SceneLayer& layer, usize room,
                                                         const hackeroot::Tables& tables);

} // namespace toki::scene
