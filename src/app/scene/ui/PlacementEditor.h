#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace toki::scene {

struct Scene;

/// Edits the placements of the open scene: the one selected in a form, and
/// adding, copying and removing them from the scene panel's menus. Every edit
/// goes through a `SceneEditor`, and what it changed is handed on to be shown.
class PlacementEditor {
public:
    /// Shows an edited scene; `hasNewActors` when it places an actor whose
    /// model was not read yet.
    using Show = std::function<void(bool hasNewActors)>;

    PlacementEditor(const workspace::WorkspaceService& workspaceService, Show show);

    void select(const PlacementRef& where) {
        selected = where;
        draft.reset();
    }

    [[nodiscard]] bool isSelected(const PlacementRef& where) const {
        return selected == where;
    }

    /// Takes the placements of `scene` as they are again, after it was read
    /// again or another layer is shown.
    void refresh(const Scene& scene);

    /// Draws the selected placement as a form, if one is selected.
    void drawInspector(Scene& scene);

    /// The items of a placement's context menu that edit it. What is picked is
    /// made by `applyPicked`, once the scene's lists are no longer drawn.
    void drawPlacementMenu(const Scene& scene, const PlacementRef& where);

    /// The item of a room's context menu that adds an actor at `position`.
    void drawRoomMenu(usize layer, usize room, const std::array<s16, 3>& position);

    /// Makes the edit picked from a menu this frame, if one was.
    void applyPicked(Scene& scene);

    /// Why the last edit was not made, or empty.
    [[nodiscard]] const std::string& getFailure() const {
        return failure;
    }

    [[nodiscard]] bool isEdited() const {
        return sceneEditor.isEdited();
    }

    [[nodiscard]] const SceneModel* findModel() const {
        return sceneEditor.findModel();
    }

    /// Forgets the edits, once they were saved or thrown away.
    void reset() {
        sceneEditor.reset();
        draft.reset();
        failure.clear();
    }

private:
    void apply(Scene& scene, const PlacementRef& where, const SceneEditor::Edit& edit);
    void add(Scene& scene, const PlacementRef& where, const Placement& placement);
    [[nodiscard]] bool editActor(ActorPlacement& placement) const;
    [[nodiscard]] bool editTransitionActor(TransitionActorPlacement& placement) const;
    [[nodiscard]] bool editActorId(s16& id) const;
    [[nodiscard]] s16 findDefaultActor() const;

    const workspace::WorkspaceService& workspaceService;
    Show show;
    SceneEditor sceneEditor;
    std::optional<PlacementRef> selected;
    std::optional<Placement> draft;
    std::string failure;
    std::function<void(Scene&)> picked;
    mutable std::string actorFilter;
};

} // namespace toki::scene
