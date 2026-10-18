#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "hackeroot/Tables.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

struct Scene;
struct SceneLayer;
struct SceneView;

/// One placement's row of the outliner, as it is handed to be drawn.
struct PlacementRow {
    PlacementRef where;
    s16 actorId = 0;
    std::array<s16, 3> position{};
    actor::PlacementValues values;
    /// What the row is called in place of the actor's name, if anything.
    std::optional<std::string> label;
    /// The objects the placement's room loads, for an actor of a room.
    std::optional<std::span<const std::string>> loadedObjects;
};

/// The Scene panel's tree of one layer: the scene, each room with its actors
/// and objects, and the scene's own lists, each row with what it holds, and a
/// room with whether it is shown. A filter keeps the placements whose name
/// holds it, and the groups that hold one.
class SceneOutliner {
public:
    struct Hooks {
        /// Draws the name cell of a placement's row: its icon and node, its
        /// tooltip and context menu, and selects it when clicked.
        std::function<void(const PlacementRow&)> drawPlacement;
        std::function<void(usize room, const RoomHeader&)> drawRoomMenu;
        /// What a placed actor is called, for the filter.
        std::function<std::string(s16 actorId)> findName;
        std::function<std::optional<PlacementRef>()> findSelected;
        std::function<bool(const SceneNode&)> isSelected;
        /// Selects the room of the layer shown when its row is clicked.
        std::function<void(usize room)> pickRoom;
        /// Shows the scene again once the view changed.
        std::function<void()> showScene;
    };

    explicit SceneOutliner(Hooks hooks) : hooks(std::move(hooks)) {}

    void draw(const Scene& scene, SceneView& view, const hackeroot::Tables* tables);

private:
    void drawRoom(const Scene& scene, const SceneLayer& layer, SceneView& view, usize room,
                  const hackeroot::Tables* tables);
    void drawObjects(const RoomHeader& header, const hackeroot::Tables* tables);
    void drawTransitionActors(const Scene& scene, const SceneLayer& layer, SceneView& view);
    void drawEntrances(const Scene& scene, const SceneLayer& layer, SceneView& view);
    void drawLightSettings(const SceneLayer& layer, SceneView& view);
    void drawPlacement(const PlacementRow& row);
    [[nodiscard]] bool isFilteredIn(s16 actorId) const;
    [[nodiscard]] bool isRevealing(PlacementKind kind, std::optional<usize> room) const;

    Hooks hooks;
    std::string filter;
    /// The selection the tree last opened its group for, so a selection made
    /// elsewhere opens it once and leaves it closable.
    std::optional<PlacementRef> revealed;
    std::optional<PlacementRef> revealing;
};

} // namespace toki::scene
