#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"

#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <variant>

namespace hackeroot {
struct Tables;
} // namespace hackeroot

namespace toki::scene {

struct Scene;

/// A placement of the open scene, where its layers hold it: `layer` indexes
/// `Scene::layers`, and `room` the layer's rooms, for a room's actor.
struct PlacementRef {
    PlacementKind kind = PlacementKind::Actor;
    usize layer = 0;
    usize room = 0;
    usize index = 0;

    friend bool operator==(const PlacementRef&, const PlacementRef&) = default;
};

/// The edits made to the open scene since it was read: a `SceneModel`, made
/// on the first edit, that every edit changes and that is shown in the scene
/// by laying its images out again.
class SceneEditor {
public:
    /// Changes the list `where` is in with `edit`, and shows the result in
    /// `scene`. An edit that fails changes nothing.
    using Edit =
        std::function<std::expected<void, base::Diagnostic>(SceneModel&, const PlacementList&)>;

    [[nodiscard]] std::expected<void, base::Diagnostic>
    apply(Scene& scene, const PlacementRef& where, const Edit& edit);

    /// Whether the scene has edits no save has written yet.
    [[nodiscard]] bool isEdited() const {
        return sceneModel && sceneModel->isEdited();
    }

    [[nodiscard]] const SceneModel* findModel() const {
        return sceneModel ? &*sceneModel : nullptr;
    }

    /// Forgets the edits, once they were saved or thrown away.
    void reset() {
        sceneModel.reset();
    }

private:
    std::optional<SceneModel> sceneModel;
};

/// A placement as the scene panel edits it: an actor or a player entry, or a
/// transition actor.
using Placement = std::variant<ActorPlacement, TransitionActorPlacement>;

/// The placement `where` names in `scene`, if it still holds one there.
[[nodiscard]] std::optional<Placement> findPlacement(const Scene& scene, const PlacementRef& where);

/// Whether `scene` places an actor its actors were not read for.
[[nodiscard]] bool hasUnreadActors(const Scene& scene);

/// The ID of the object named `object`, when the room of the actor `where`
/// loads no object of that name in its layer: neither a keep object nor one of
/// its object list. The game spawns no actor whose object is not loaded.
[[nodiscard]] std::optional<s16> findMissingObject(const Scene& scene, const PlacementRef& where,
                                                   std::string_view object,
                                                   const hackeroot::Tables& tables);

/// The list of the scene's placements `where` is in, as the edits name it.
[[nodiscard]] std::optional<PlacementList> findPlacementList(const Scene& scene,
                                                             const PlacementRef& where);

} // namespace toki::scene
