#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Panel.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneFlags.h"
#include "app/scene/domain/SceneView.h"
#include "app/scene/ui/PlacementEditor.h"
#include "app/scene/ui/SceneOutliner.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace toki::scene {

/// The open scene: which of its layers and light settings the viewport shows,
/// which rooms, what is placed in them, and the scene flags its actors share.
class ScenePanel : public core::Panel {
public:
    ScenePanel(const workspace::WorkspaceService& workspaceService,
               viewport::ViewportPanel& viewportPanel, actor::DatabaseStore& databaseStore,
               actor::CollectionCatalog& collectionCatalog);

    /// Shows `scene` in the viewport. A scene read again keeps how it was
    /// shown and where the camera is; another one starts afresh at its first
    /// entrance. While the open scene has edits no save wrote, the person is
    /// asked first whether to throw them away.
    void setScene(Scene read);

    /// The open scene, if there is one.
    [[nodiscard]] const Scene* findScene() const {
        return scene ? &*scene : nullptr;
    }

    /// The open scene's edits no save has written yet, if it has any.
    [[nodiscard]] const SceneModel* findEditedModel() const {
        return placementEditor.isEdited() ? placementEditor.findModel() : nullptr;
    }

    /// Takes the open scene's edits as written, so its files read again
    /// replace it without asking.
    void markSaved() {
        placementEditor.reset();
    }

    /// What finds the object an actor needs, by its ID, as the actor database
    /// says, so that a room an actor is placed in loads it.
    void setActorObjectFinder(std::function<std::optional<std::string>(s16 actorId)> finder) {
        placementEditor.setObjectFinder(std::move(finder));
    }

    /// What finds the name the actor database gives an actor, by its enum name.
    void setActorNameFinder(ActorForm::FindName finder) {
        placementEditor.setNameFinder(std::move(finder));
    }

    /// What reads the actors an edit placed that were not read yet.
    void setActorReader(std::function<void(const Scene&)> reader) {
        readActors = std::move(reader);
    }

    /// Draws the open scene's actors as `actors` says, when they have been read.
    void setActors(SceneActors actors);

    /// Forgets the open scene, when something else takes the viewport.
    void clearScene() {
        scene.reset();
        heldScene.reset();
        placementEditor.reset();
        sceneView = {};
        previewPaths.reset();
        pendingSession.reset();
        viewportPanel.setOverlay({});
        viewportPanel.clearChoices();
        viewportPanel.setPickables({}, {});
        viewportPanel.setTransformTarget(std::nullopt, {});
    }

    /// How the open scene is shown, to open it again the same way, or nothing
    /// when no scene is open.
    [[nodiscard]] std::optional<core::SceneSession> makeSession() const;

    /// Shows the scene `session` names the way it says, camera included, when
    /// that scene next arrives through `setScene`.
    void restoreSession(core::SceneSession session) {
        pendingSession = std::move(session);
    }

    /// What "Edit actor" opens an actor's entry with, by its enum name.
    void setActorEditor(std::function<void(const std::string&)> editor) {
        placementEditor.setActorEditor(editor);
        openActorEditor = std::move(editor);
    }

    /// What "Inspect in Display List" opens a room's mesh with, by its name.
    void setDisplayListInspector(std::function<void(const std::string&)> inspector) {
        inspectDisplayList = std::move(inspector);
    }

    /// Shows placements' rotations as `format`; `changed` is told when the
    /// panel switches it.
    void setAngleFormat(core::AngleFormat format, std::function<void(core::AngleFormat)> changed) {
        placementEditor.setAngleFormat(format, std::move(changed));
    }

    /// Marks actors not drawn as their models with `shape` from now on.
    void setMarkerShape(core::MarkerShape shape) {
        markerShape = shape;
        showScene();
    }

    void draw() override;

    /// Draws the properties of what is selected, for the Properties panel.
    void drawProperties();

    /// Makes the edits picked this frame, wherever they were picked, and shows
    /// the scene again once a preview of a transform ended. Called every frame
    /// after the panels are drawn, whichever of them are open.
    void update();

    /// Takes back the last edit of the scene, or puts back the last one taken
    /// back.
    void undo() {
        if (scene) {
            placementEditor.undo(*scene);
        }
    }

    void redo() {
        if (scene) {
            placementEditor.redo(*scene);
        }
    }

    [[nodiscard]] bool canUndo() const {
        return scene && placementEditor.canUndo();
    }

    [[nodiscard]] bool canRedo() const {
        return scene && placementEditor.canRedo();
    }

private:
    void showScene();
    void offerChoices();
    void drawPickers(const Scene& shown);
    void drawRoomMenu(usize room, const RoomHeader& header);
    void drawHeldScene();
    /// Offers the viewport the placements it shows to pick, and the selected
    /// one to move.
    void offerPlacements(const SceneLayer& layer);
    void drawFlags(const std::vector<SceneFlagUse>& flags);
    /// Hands the viewport its overlay: the links between actors sharing a
    /// flag, and the layer's paths.
    void drawOverlay(const std::vector<SceneFlagUse>& flags, const SceneLayer& layer);
    void addFlagLinks(viewport::ViewportOverlay& overlay, const std::vector<SceneFlagUse>& flags);
    void addPathLines(viewport::ViewportOverlay& overlay, const std::vector<ScenePath>& paths);
    void addFollowerLines(viewport::ViewportOverlay& overlay, const std::vector<ScenePath>& paths);
    void drawFlagUser(const FlagUser& user);
    /// The name cell of one placement's row of the outliner.
    void drawActor(const PlacementRow& row);
    /// Selects `node` as a click in the tree does: Ctrl toggles it and Shift
    /// adds it. True when it alone is selected now.
    [[nodiscard]] bool pickInTree(const SceneNode& node);
    /// Selects a light setting, path or point clicked in the tree, and
    /// lights the scene with the setting or turns the camera to the path.
    void pickListNode(const SceneNode& node);
    void drawNodeMenu(const SceneNode& node);
    void drawGroupMenu(OutlinerGroup group);
    void showPreview(const std::vector<SceneNode>& nodes,
                     const std::optional<viewport::TransformEdit>& edit);
    /// Shows the scene with what `preview` holds in place of what the scene
    /// holds there.
    void showEdited(const PlacementEditor::Preview& preview);
    /// Shows `layer` in place of the shown layer of the scene, as `view` says.
    void showLayer(const SceneLayer& layer, const SceneView& view);
    [[nodiscard]] std::array<s16, 3> findCameraTarget() const;
    [[nodiscard]] std::string findOutlinerName(s16 actorId) const;
    void drawActorTooltip(s16 actorId, const std::array<s16, 3>& position,
                          const ActorDescription& description) const;
    void drawActorMenu(const SceneActorModel& model, const actor::PlacementValues& placement,
                       const ActorDescription& description);
    [[nodiscard]] std::string findActorName(s16 actorId) const;

    const workspace::WorkspaceService& workspaceService;
    viewport::ViewportPanel& viewportPanel;
    actor::DatabaseStore& databaseStore;
    actor::CollectionCatalog& collectionCatalog;
    std::function<void(const std::string&)> openActorEditor;
    std::optional<Scene> scene;
    SceneView sceneView;
    core::MarkerShape markerShape = core::MarkerShape::Cube;
    std::optional<core::SceneSession> pendingSession;
    std::optional<std::pair<actor::SceneFlag, s64>> hoveredFlag;
    std::function<void(const std::string&)> inspectDisplayList;
    std::function<void(const Scene&)> readActors;
    PlacementEditor placementEditor;
    /// Whether a preview of a transform ended this frame, so the scene is
    /// shown as it is again.
    bool isPreviewEnding = false;
    /// The edits of the Properties panel the viewport shows, before they are
    /// made.
    PlacementEditor::Preview shownPreview;
    /// The paths of the layer a preview shows, which the overlay draws in
    /// place of the layer's own until the scene is shown as it is again.
    std::optional<std::vector<ScenePath>> previewPaths;
    SceneOutliner sceneOutliner;
    /// A scene read while the open one had edits no save wrote, until the
    /// person says whether to throw them away for it.
    std::optional<Scene> heldScene;
};

} // namespace toki::scene
