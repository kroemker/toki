#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/ui/ActorForm.h"
#include "app/scene/ui/ParamsForm.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
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

    PlacementEditor(const workspace::WorkspaceService& workspaceService,
                    actor::CollectionCatalog& collectionCatalog, Show show);

    /// What finds the object the actor database says an actor needs, by its
    /// ID, so that the room it is placed in loads it.
    void setObjectFinder(std::function<std::optional<std::string>(s16 actorId)> finder) {
        findActorObject = std::move(finder);
    }

    /// Shows rotations as `format`; `changed` is told when the form's own
    /// button switches it.
    void setAngleFormat(core::AngleFormat format, std::function<void(core::AngleFormat)> changed) {
        actorForm.setAngleFormat(format, std::move(changed));
    }

    /// What "Edit in the Actor Database" opens an actor's entry with, by its
    /// enum name.
    void setActorEditor(std::function<void(const std::string&)> editor) {
        openActorEditor = std::move(editor);
    }

    void select(const PlacementRef& where) {
        selected = where;
        draft.reset();
    }

    [[nodiscard]] bool isSelected(const PlacementRef& where) const {
        return selected == where;
    }

    [[nodiscard]] const std::optional<PlacementRef>& findSelected() const {
        return selected;
    }

    /// Takes the placements of `scene` as they are again, after it was read
    /// again or another layer is shown.
    void refresh(const Scene& scene);

    /// Draws the selected placement's properties: the actor as stored, its
    /// params as the actor database names them, and the object it needs.
    void drawInspector(Scene& scene);

    /// The items of a placement's context menu that edit it. What is picked is
    /// made by `applyPicked`, once the scene's lists are no longer drawn.
    void drawPlacementMenu(const Scene& scene, const PlacementRef& where);

    /// The item of a room's context menu that adds an actor at `position`.
    void drawRoomMenu(usize layer, usize room, const std::array<s16, 3>& position);

    /// Makes the edit picked from a menu this frame, if one was.
    void applyPicked(Scene& scene);

    /// Where the selected placement is, as it is being edited.
    [[nodiscard]] std::optional<std::array<s16, 3>> findSelectedPosition(const Scene& scene) const;

    /// Moves the selected placement to `position`, as a gizmo drags it; once
    /// `isDone`, the move is made by `applyPicked`.
    void moveSelected(const Scene& scene, const std::array<s16, 3>& position, bool isDone);

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
    bool apply(Scene& scene, const PlacementRef& where, const SceneEditor::Edit& edit);
    void addObjectFor(Scene& scene, const PlacementRef& where, s16 actorId);
    void addObject(Scene& scene, const PlacementRef& where, const std::string& object);
    void add(Scene& scene, const PlacementRef& where, const Placement& placement);
    void pickDuplicate(const PlacementRef& where, const Placement& placement);
    void pickDelete(const PlacementRef& where);
    void drawHeader(const PlacementRef& where, s16 actorId, const actor::ActorEntry* entry) const;
    [[nodiscard]] bool drawParams(const Scene& scene, const PlacementRef& where,
                                  const actor::ActorEntry* entry, s16 actorId);
    void drawObject(const Scene& scene, const PlacementRef& where, const actor::ActorEntry& entry);
    void drawActions(const PlacementRef& where);
    void commitDraft(Scene& scene, const PlacementRef& where);
    [[nodiscard]] s16 findDefaultActor() const;

    const workspace::WorkspaceService& workspaceService;
    actor::CollectionCatalog& collectionCatalog;
    Show show;
    ActorForm actorForm;
    ParamsForm paramsForm;
    std::function<void(const std::string&)> openActorEditor;
    std::function<std::optional<std::string>(s16)> findActorObject;
    SceneEditor sceneEditor;
    std::optional<PlacementRef> selected;
    std::optional<Placement> draft;
    std::string failure;
    std::function<void(Scene&)> picked;
};

} // namespace toki::scene
