#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/scene/ui/ActorForm.h"
#include "app/scene/ui/CollisionForm.h"
#include "app/scene/ui/HeaderForm.h"
#include "app/scene/ui/LightSettingForm.h"
#include "app/scene/ui/ParamsForm.h"
#include "app/scene/ui/PathForm.h"
#include "app/scene/ui/WaterBoxForm.h"
#include "app/viewport/ui/ViewportTransform.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace toki::scene {

struct Scene;

/// Edits the placements of the open scene: the one selected in a form, and
/// adding, copying and removing them from the scene panel's menus, and what
/// else is selected in its own form: a header, a light setting or a path.
/// Every edit goes through a `SceneEditor`, and what it changed is handed on
/// to be shown.
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
    void setNameFinder(ActorForm::FindName finder) {
        paramsForm.setNameFinder(finder);
        actorForm.setNameFinder(std::move(finder));
    }

    void setCollisionNames(const CollisionNames* names) {
        collisionForm.setNames(names);
    }

    void setAngleFormat(core::AngleFormat format, std::function<void(core::AngleFormat)> changed) {
        actorForm.setAngleFormat(format, std::move(changed));
    }

    /// What "Edit in the Actor Database" opens an actor's entry with, by its
    /// enum name.
    void setActorEditor(std::function<void(const std::string&)> editor) {
        openActorEditor = std::move(editor);
    }

    void select(const PlacementRef& where) {
        selection.select(where);
        draft.reset();
    }

    /// Changes the selection by `nodes` picked as `mode` says.
    void pick(std::span<const SceneNode> nodes, PickMode mode) {
        selection.pick(nodes, mode);
        draft.reset();
    }

    [[nodiscard]] bool isSelected(const SceneNode& node) const {
        return selection.contains(node);
    }

    /// The placement selected last, which the Properties panel shows.
    [[nodiscard]] std::optional<PlacementRef> findSelected() const;

    /// A placement as the Properties panel holds it while it is edited.
    struct Draft {
        PlacementRef where;
        Placement placement;

        friend bool operator==(const Draft&, const Draft&) = default;
    };

    /// The placement being edited, as it is now, while that differs from
    /// what `scene` holds; nothing otherwise.
    [[nodiscard]] std::optional<Draft> findChangedDraft(const Scene& scene) const;

    /// What the Properties panel holds while it is edited, each draft while
    /// it differs from what the scene holds, for the viewport to show before
    /// the edit is made.
    struct Preview {
        std::optional<Draft> placement;
        std::optional<LightSettingForm::Draft> lightSetting;
        std::optional<PathForm::Draft> path;

        [[nodiscard]] bool isEmpty() const {
            return !placement && !lightSetting && !path;
        }

        friend bool operator==(const Preview&, const Preview&) = default;
    };

    [[nodiscard]] Preview findPreview(const Scene& scene) const;

    [[nodiscard]] const SceneSelection& getSelection() const {
        return selection;
    }

    /// Takes the placements of `scene` as they are again, after it was read
    /// again or another layer is shown.
    void refresh(const Scene& scene);

    /// Draws the selected placement's properties: the actor as stored, its
    /// params as the actor database names them, and the object it needs. The
    /// scene or a room selected shows its header's settings instead.
    void drawInspector(Scene& scene);

    /// The items of a placement's context menu that edit it. What is picked is
    /// made by `applyPicked`, once the scene's lists are no longer drawn.
    void drawPlacementMenu(const Scene& scene, const PlacementRef& where);

    /// The item of a room's context menu that adds an actor at `position`.
    void drawRoomMenu(usize layer, usize room, const std::array<s16, 3>& position);

    /// Makes the edit picked from a menu this frame, if one was.
    void applyPicked(Scene& scene);

    /// The selected placements and path points as the viewport transforms
    /// them, and which they are, in the order of the target's points.
    struct SelectionTransform {
        viewport::TransformTarget target;
        std::vector<SceneNode> nodes;
    };

    [[nodiscard]] std::optional<SelectionTransform> makeTransformTarget(const Scene& scene) const;

    /// Moves and turns `nodes` as a confirmed transform says, by
    /// `applyPicked`, as one step. A path point is moved, never turned.
    void applyTransform(std::vector<SceneNode> nodes, const viewport::TransformEdit& edit);

    /// Makes `edit` by `applyPicked`, as one step.
    void pickSceneEdit(SceneEditor::ModelEdit edit) {
        pickModelEdit(std::move(edit), std::nullopt);
    }

    [[nodiscard]] LightSettingForm& getLightSettingForm() {
        return lightSettingForm;
    }

    [[nodiscard]] PathForm& getPathForm() {
        return pathForm;
    }

    [[nodiscard]] CollisionForm& getCollisionForm() {
        return collisionForm;
    }

    [[nodiscard]] WaterBoxForm& getWaterBoxForm() {
        return waterBoxForm;
    }

    /// Why the last edit was not made, or empty.
    [[nodiscard]] const std::string& getFailure() const {
        return failure;
    }

    [[nodiscard]] bool isEdited() const {
        return sceneEditor.isEdited();
    }

    void undo(Scene& scene);
    void redo(Scene& scene);

    [[nodiscard]] bool canUndo() const {
        return sceneEditor.canUndo();
    }

    [[nodiscard]] bool canRedo() const {
        return sceneEditor.canRedo();
    }

    [[nodiscard]] const SceneModel* findModel() const {
        return sceneEditor.findModel();
    }

    /// Forgets the edits, once they were saved or thrown away.
    void reset() {
        sceneEditor.reset();
        resetDrafts();
        failure.clear();
    }

private:
    bool apply(Scene& scene, const PlacementRef& where, const SceneEditor::Edit& edit,
               bool joinsLast = false);
    bool applyToModel(Scene& scene, const SceneEditor::ModelEdit& edit);
    /// Makes `edit` by `applyPicked`, and selects `select` when it was made.
    void pickModelEdit(SceneEditor::ModelEdit edit, std::optional<SceneNode> select);
    void resetDrafts();
    [[nodiscard]] bool applyPathMoves(Scene& scene, std::span<const SceneNode> nodes,
                                      std::span<const glm::vec3> points, bool joinsLast);
    [[nodiscard]] bool applyWaterBoxMoves(Scene& scene, std::span<const SceneNode> nodes,
                                          std::span<const glm::vec3> points, bool joinsLast);
    /// Takes what an edit did: shows the scene, or keeps why it failed.
    bool showApplied(Scene& scene, const std::expected<void, base::Diagnostic>& applied);
    void showChanged(Scene& scene);
    void addObjectFor(Scene& scene, const PlacementRef& where, s16 actorId);
    void addObject(Scene& scene, const PlacementRef& where, const std::string& object,
                   bool joinsLast = false);
    std::optional<PlacementRef> add(Scene& scene, const PlacementRef& where,
                                    const Placement& placement, bool joinsLast = false);
    /// What a menu or button opened on `where` acts on: the selection when
    /// `where` is in it, or else only `where`.
    [[nodiscard]] std::vector<PlacementRef> listActedOn(const PlacementRef& where) const;
    void pickDuplicate(std::vector<PlacementRef> placements);
    void pickDelete(std::vector<PlacementRef> placements);
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
    HeaderForm headerForm;
    LightSettingForm lightSettingForm;
    PathForm pathForm;
    CollisionForm collisionForm;
    WaterBoxForm waterBoxForm;
    std::function<void(const std::string&)> openActorEditor;
    std::function<std::optional<std::string>(s16)> findActorObject;
    SceneEditor sceneEditor;
    SceneSelection selection;
    std::optional<Placement> draft;
    std::string failure;
    std::function<void(Scene&)> picked;
};

} // namespace toki::scene
