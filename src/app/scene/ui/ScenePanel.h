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
        pendingSession.reset();
        viewportPanel.setOverlay({});
        viewportPanel.clearChoices();
        viewportPanel.setPickables({}, {});
        viewportPanel.setHandle(std::nullopt, {});
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
        openActorEditor = std::move(editor);
    }

    /// What "Inspect in Display List" opens a room's mesh with, by its name.
    void setDisplayListInspector(std::function<void(const std::string&)> inspector) {
        inspectDisplayList = std::move(inspector);
    }

    /// Marks actors not drawn as their models with `shape` from now on.
    void setMarkerShape(core::MarkerShape shape) {
        markerShape = shape;
        showScene();
    }

    void draw() override;

private:
    void showScene();
    void offerChoices();
    void drawPickers(const Scene& shown);
    void drawRooms(const SceneLayer& layer);
    void drawRoomMenu(usize room, const RoomHeader& header);
    void drawHeldScene();
    /// Offers the viewport the placements it shows to pick, and the selected
    /// one to move.
    void offerPlacements(const SceneLayer& layer);
    void drawPlacements(const SceneLayer& layer);
    void drawFlags(const std::vector<SceneFlagUse>& flags);
    void drawFlagLinks(const std::vector<SceneFlagUse>& flags);
    void drawFlagUser(const FlagUser& user);
    /// One placed actor's row; true when it was clicked.
    bool drawActor(const PlacementRef& where, s16 actorId, const std::array<s16, 3>& position,
                   const actor::PlacementValues& placement, const char* label,
                   std::optional<std::span<const std::string>> loadedObjects);
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
    /// A scene read while the open one had edits no save wrote, until the
    /// person says whether to throw them away for it.
    std::optional<Scene> heldScene;
};

} // namespace toki::scene
