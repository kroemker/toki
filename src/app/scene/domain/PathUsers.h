#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

struct Scene;

/// A placed actor that follows one of its layer's paths by a param of its
/// entry: which placement it is, which actor, where it is, the param, and the
/// path's index.
struct PathUser {
    PlacementRef where;
    s16 actorId = 0;
    std::array<s16, 3> position{};
    std::string param;
    usize path = 0;
};

/// The path `param` names for `placement`, or nothing when the param is no
/// path, or at its highest value, which the actors that check for none take
/// to mean none.
[[nodiscard]] std::optional<s64> findPathNumber(const actor::ActorParam& param,
                                                const actor::PlacementValues& placement);

/// Every actor placed in `layer` of `scene`, an index into `Scene::layers`,
/// that follows one of the layer's paths, in its rooms and between them.
[[nodiscard]] std::vector<PathUser> listPathUsers(const Scene& scene, usize layer);

/// What becomes of the actors that follow the layer's paths when the path at
/// `path` is removed: one that follows a path after it names the path one
/// lower. One that follows the removed path is left as it is, since not every
/// actor takes any value to mean none: it follows the path that moves into
/// its place. The actors of a room header another layer reads too are left as
/// they are, since that layer keeps its paths.
[[nodiscard]] std::vector<PlacementChange> listPathRemovalChanges(const Scene& scene, usize layer,
                                                                  usize path);

} // namespace toki::scene
