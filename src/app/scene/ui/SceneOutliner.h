#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/scene/Types.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "hackeroot/Tables.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

struct Scene;
struct SceneLayer;
struct SceneMaterial;
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

/// A group of the outliner whose context menu adds to it.
enum class OutlinerGroup : u8 {
    LightSettings,
    Paths,
    SurfaceTypes,
    WaterBoxes,
};

/// The Scene panel's tree of one layer: the scene, each room with its actors
/// and objects, and the scene's own lists, each row with what it holds, and a
/// room and the paths with whether they are shown. A filter keeps the placements whose name
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
        /// Selects the scene when its row is clicked.
        std::function<void()> pickScene;
        /// Shows the scene again once the view changed.
        std::function<void()> showScene;
        /// Selects a light setting, a path or a point when its row is clicked.
        std::function<void(const SceneNode&)> pickNode;
        /// Selects a waterbox when its row is clicked.
        std::function<void(std::span<const SceneNode>)> pickNodes;
        /// Draws the items of a light setting's, a path's or a point's
        /// context menu.
        std::function<void(const SceneNode&)> drawNodeMenu;
        std::function<void(OutlinerGroup)> drawGroupMenu;
        /// The materials the rooms are drawn with, once the mesh was read.
        std::function<const std::vector<SceneMaterial>*()> findMaterials;
        /// Selects a material's faces when its row is clicked.
        std::function<void(usize material)> pickMaterial;
        std::function<void(usize material)> drawMaterialMenu;
        /// Selects the faces and polygons made of a collision material when its
        /// row is clicked.
        std::function<void(const CollisionMaterial&)> pickCollisionMaterial;
        /// What a material is bound to, named, if it is.
        std::function<std::optional<std::string>(usize material)> findBindingName;
    };

    explicit SceneOutliner(Hooks hooks) : hooks(std::move(hooks)) {}

    void draw(const Scene& scene, SceneView& view, const hackeroot::Tables* tables);

    /// Names surface types' settings in their rows' tooltips as `names` does.
    void setCollisionNames(const CollisionNames* names) {
        collisionNames = names;
    }

private:
    void drawRoom(const Scene& scene, const SceneLayer& layer, SceneView& view, usize room,
                  const hackeroot::Tables* tables);
    void drawObjects(const RoomHeader& header, const hackeroot::Tables* tables);
    void drawRoomsShown(SceneView& view) const;
    void drawTransitionActors(const Scene& scene, const SceneLayer& layer, SceneView& view);
    void drawEntrances(const Scene& scene, const SceneLayer& layer, SceneView& view);
    void drawLightSettings(const SceneLayer& layer, SceneView& view);
    void drawPaths(const SceneLayer& layer, SceneView& view);
    void drawPathPoints(const ScenePath& path, const SceneNode& pathNode);
    void drawWaterBoxes(const Scene& scene, SceneView& view);
    void drawMaterials(const Scene& scene, const SceneView& view);
    void drawRenderMaterials();
    void drawCollisionMaterials(const Collision& collision, const SceneView& view);
    /// The eye at the end of a group's row that shows or hides `what` in the
    /// viewport.
    void drawShownSwitch(bool& isShown, std::string_view what) const;
    void drawCameras(const Scene& scene, const SceneView& view) const;
    void drawGroupMenu(OutlinerGroup group) const;
    void drawNodeMenu(const SceneNode& node) const;
    void drawPlacement(const PlacementRow& row);
    [[nodiscard]] bool isFilteredIn(s16 actorId) const;
    [[nodiscard]] bool isRevealing(PlacementKind kind, std::optional<usize> room) const;

    Hooks hooks;
    const CollisionNames* collisionNames = nullptr;
    std::string filter;
    /// The selection the tree last opened its group for, so a selection made
    /// elsewhere opens it once and leaves it closable.
    std::optional<PlacementRef> revealed;
    std::optional<PlacementRef> revealing;
    /// The same for the surface type of the faces picked.
    std::optional<u16> revealedSurface;
    std::optional<u16> revealingSurface;
};

} // namespace toki::scene
