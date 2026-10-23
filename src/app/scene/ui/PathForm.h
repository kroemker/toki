#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"

#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace toki::scene {

struct Scene;

/// The Properties panel for a path or one of its points selected, and the
/// menus that add and remove paths and points. A path's points are edited as
/// a draft the viewport shows, and made once nothing of the form is held.
class PathForm {
public:
    /// Makes `edit` as one step once the panels are drawn, and selects
    /// `select` when it was made.
    using Pick = std::function<void(SceneEditor::ModelEdit edit, std::optional<SceneNode> select)>;

    explicit PathForm(Pick pick) : pick(std::move(pick)) {}

    /// A path as the form holds it while it is edited.
    struct Draft {
        PathRef where;
        ScenePath path;

        friend bool operator==(const Draft&, const Draft&) = default;
    };

    /// Draws the path `where` is, or is a point of. `selected` is how many
    /// nodes are selected, of which this is the last.
    void draw(const Scene& scene, const SceneNode& where, usize selected);

    /// The items of a path's or a point's context menu.
    void drawMenu(const Scene& scene, const SceneNode& where);

    /// The items of the paths' group's context menu, for `layer`, an index
    /// into `Scene::layers`: a path of one point at `position`.
    void drawGroupMenu(const Scene& scene, usize layer, const std::array<s16, 3>& position);

    /// The path being edited, as it is now, while that differs from what
    /// `scene` holds; nothing otherwise.
    [[nodiscard]] std::optional<Draft> findChangedDraft(const Scene& scene) const;

    /// Takes the path as the scene holds it again, after an edit.
    void reset() {
        draft.reset();
    }

private:
    void drawPoints(const Scene& scene, const PathRef& path, std::optional<usize> selectedPoint);
    void pickPoints(const Scene& scene, const PathRef& path, ScenePath points,
                    std::optional<SceneNode> select);
    void pickInsert(const Scene& scene, const PathRef& path, usize after);
    void pickRemovePoint(const Scene& scene, const PathPointRef& point);
    void pickRemove(const Scene& scene, const PathRef& path);

    Pick pick;
    std::optional<Draft> draft;
};

/// The path `where` is, or is a point of.
[[nodiscard]] PathRef findPathOf(const SceneNode& where);

} // namespace toki::scene
