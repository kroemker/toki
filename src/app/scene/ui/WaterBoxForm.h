#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/scene/ui/CollisionRows.h"

#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace toki::scene {

struct Scene;

/// The Properties panel for a waterbox selected, each of its values edited in
/// place, and the menus that add, duplicate and delete waterboxes.
class WaterBoxForm {
public:
    /// Makes `edit` as one step once the panels are drawn, and selects
    /// `select` when it was made.
    using Pick = std::function<void(SceneEditor::ModelEdit edit, std::optional<SceneNode> select)>;

    explicit WaterBoxForm(Pick pick) : pick(std::move(pick)) {}

    /// `selected` is how many nodes are selected, of which this is the last.
    void draw(const Scene& scene, const WaterBoxRef& where, usize selected);

    /// The items of a waterbox's context menu.
    void drawMenu(const Scene& scene, const WaterBoxRef& where);

    /// The items of the waterboxes' group menu, for `layer`, an index into
    /// `Scene::layers`: a waterbox with its surface at `position`.
    void drawGroupMenu(const Scene& scene, usize layer, const std::array<s16, 3>& position);

private:
    void pickBox(const Scene& scene, const WaterBoxRef& where, const WaterBox& box);
    void pickAdd(const Scene& scene, usize layer, const WaterBox& box);

    Pick pick;
    CollisionRows collisionRows;
};

} // namespace toki::scene
