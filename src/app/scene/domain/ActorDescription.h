#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/FieldReading.h"
#include "app/actor/domain/SpawnCheck.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// What the Scene panel says of one placed actor: its name and its entry's
/// status, its params read by the entry's fields, whether it spawns in the
/// layer shown and what it needs to, where its entry comes from, what the
/// variant its placement matches draws, what keeps it from being drawn fully,
/// and which edits of the checkout's actor database fit it.
struct ActorDescription {
    bool isDrawn = false;
    std::string name;
    /// `checked`, `guessed`, `marker` or `none`.
    std::string status;
    std::vector<actor::FieldReading> fields;
    actor::SpawnCheck spawn;
    std::vector<std::string> lines;
    std::vector<std::string> problems;
    /// Whether its entry is a guess that "Keep this guess" can write.
    bool canKeepGuess = false;
    /// Whether its entry has fields that "Add a variant for these params" can
    /// match on.
    bool canAddVariant = false;
};

/// Describes `actor` where it is placed with `placement`, in a layer at `time`.
/// `loadedObjects`, when given, are the objects the game has loaded there, the
/// room's object list and the keep objects: an actor whose entry's object is
/// not among them is not spawned by the game, and is said so.
[[nodiscard]] ActorDescription
describeActor(const SceneActorModel& actor, const actor::PlacementValues& placement,
              std::optional<actor::TimeOfDay> time,
              std::optional<std::span<const std::string>> loadedObjects);

/// The objects the game has loaded while `room` of `layer` is: `gameplay_keep`,
/// the scene's keep object, and the room's object list, by name.
[[nodiscard]] std::vector<std::string> listLoadedObjects(const SceneLayer& layer, usize room,
                                                         const hackeroot::Tables& tables);

} // namespace toki::scene
