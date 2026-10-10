#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ParamReading.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// What the Scene panel says of one placed actor: its name, its params read by
/// its entry, where its entry comes from,
/// what the variant its placement matches draws, what keeps it from being
/// drawn fully, and which edits of the checkout's actor database fit it.
struct ActorDescription {
    bool isDrawn = false;
    std::string name;
    std::vector<actor::ParamReading> params;
    std::vector<std::string> lines;
    std::vector<std::string> problems;
    /// Whether its entry has params that "Add a variant for these params" can
    /// match on.
    bool canAddVariant = false;
};

/// Describes `actor` where it is placed with `placement`.
/// `loadedObjects`, when given, are the objects the game has loaded there, the
/// room's object list and the keep objects: an actor whose entry's object is
/// not among them is not spawned by the game, and is said so. Choices of
/// collections are named by what `findCollection` finds.
[[nodiscard]] ActorDescription
describeActor(const SceneActorModel& actor, const actor::PlacementValues& placement,
              std::optional<std::span<const std::string>> loadedObjects,
              const actor::FindCollection& findCollection);

/// The objects the game has loaded while `room` of `layer` is: `gameplay_keep`,
/// the scene's keep object, and the room's object list, by name.
[[nodiscard]] std::vector<std::string> listLoadedObjects(const SceneLayer& layer, usize room,
                                                         const hackeroot::Tables& tables);

} // namespace toki::scene
