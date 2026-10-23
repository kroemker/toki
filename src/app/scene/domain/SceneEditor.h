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

    /// Each edit is a step undone at once, unless it `joinsLast`, as the
    /// object an actor needs is added with it.
    [[nodiscard]] std::expected<void, base::Diagnostic>
    apply(Scene& scene, const PlacementRef& where, const Edit& edit, bool joinsLast = false);

    /// Changes the model with `edit`, which knows itself what it changes, as
    /// `apply` does.
    using ModelEdit = std::function<std::expected<void, base::Diagnostic>(SceneModel&)>;

    [[nodiscard]] std::expected<void, base::Diagnostic>
    applyToModel(Scene& scene, const ModelEdit& edit, bool joinsLast = false);

    /// Takes back the last step, or puts back the last step taken back, and
    /// shows the result in `scene`. False when there is none.
    bool undo(Scene& scene);
    bool redo(Scene& scene);

    [[nodiscard]] bool canUndo() const {
        return !undone.empty();
    }

    [[nodiscard]] bool canRedo() const {
        return !redone.empty();
    }

    /// How many steps are kept to take back; the oldest are dropped.
    static constexpr usize HistoryLength = 64;

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
        undone.clear();
        redone.clear();
    }

private:
    std::optional<SceneModel> sceneModel;
    /// The model before each step, the last step's last.
    std::vector<SceneModel> undone;
    std::vector<SceneModel> redone;
};

/// A placement as the scene panel edits it: an actor or a player entry, or a
/// transition actor.
using Placement = std::variant<ActorPlacement, TransitionActorPlacement>;

/// The placement `where` names in `scene`, if it still holds one there.
[[nodiscard]] std::optional<Placement> findPlacement(const Scene& scene, const PlacementRef& where);

/// The light setting of a layer `layer` and `index` name, an index into
/// `Scene::layers` and one into its light settings, if it still holds one.
[[nodiscard]] std::optional<LightSetting> findLightSetting(const Scene& scene, usize layer,
                                                           usize index);

/// The path of a layer `layer` and `index` name, as for a light setting.
[[nodiscard]] std::optional<ScenePath> findPath(const Scene& scene, usize layer, usize index);

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
