#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"

#include <expected>
#include <span>

namespace toki::scene {

class SceneModel;

/// One of the lists of placements a header points at.
enum class PlacementKind : u8 {
    /// A room's `ActorEntry` list.
    Actor,
    /// The scene's `TransitionActorEntry` list.
    TransitionActor,
    /// The scene's `ActorEntry` list of where the player enters.
    PlayerEntry,
};

/// Which list an edit changes: the list of `kind` the header of `layer`
/// points at, in `room` for a room's actors. A room with no header of its own
/// for the layer is shown its main header, as the game falls back to it, and
/// so is edited there.
///
/// An edit changes the bytes of the list the header points at. A list another
/// header points at too is copied for this header first, so an edit of one
/// layer never shows in another that shared its list.
struct PlacementList {
    PlacementKind kind = PlacementKind::Actor;
    usize layer = 0;
    usize room = 0;
};

/// Sets the placement at `index` of an actor or player entry list.
[[nodiscard]] std::expected<void, base::Diagnostic> setPlacement(SceneModel& model,
                                                                 const PlacementList& list,
                                                                 usize index,
                                                                 const ActorPlacement& placement);

/// Sets the transition actor at `index` of a layer, keeping the camera each of
/// its sides uses.
[[nodiscard]] std::expected<void, base::Diagnostic>
setTransitionActor(SceneModel& model, usize layer, usize index,
                   const TransitionActorPlacement& placement);

/// Adds `placement` to the end of a room's actor list, and returns its index.
/// A header with no actor list cannot be added to yet.
[[nodiscard]] std::expected<usize, base::Diagnostic>
addPlacement(SceneModel& model, const PlacementList& list, const ActorPlacement& placement);

/// Adds a transition actor to the end of a layer's list, and returns its index.
[[nodiscard]] std::expected<usize, base::Diagnostic>
addTransitionActor(SceneModel& model, usize layer, const TransitionActorPlacement& placement);

/// Adds `objects`, by ID, to the end of the object list of `room`'s header for
/// `layer`, the game's scene layer, as for its actors.
[[nodiscard]] std::expected<void, base::Diagnostic>
addRoomObjects(SceneModel& model, usize layer, usize room, std::span<const s16> objects);

/// Removes the placement at `index`. Removing the last one removes the list's
/// command from the header, and the list itself once nothing points at it.
/// Player entries are not removed: the spawn list names them by index.
[[nodiscard]] std::expected<void, base::Diagnostic>
removePlacement(SceneModel& model, const PlacementList& list, usize index);

} // namespace toki::scene
