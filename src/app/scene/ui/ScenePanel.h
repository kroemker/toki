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
    /// entrance.
    void setScene(Scene read);

    /// Draws the open scene's actors as `actors` says, when they have been read.
    void setActors(SceneActors actors);

    /// Forgets the open scene, when something else takes the viewport.
    void clearScene() {
        scene.reset();
        sceneView = {};
        pendingSession.reset();
        viewportPanel.setOverlay({});
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

    void draw() override;

private:
    void showScene();
    void drawPickers(const Scene& shown);
    void drawRooms(const SceneLayer& layer);
    void drawPlacements(const SceneLayer& layer);
    void drawFlags(const std::vector<SceneFlagUse>& flags);
    void drawFlagLinks(const std::vector<SceneFlagUse>& flags);
    void drawFlagUser(const FlagUser& user);
    /// One placed actor's row; true when it was clicked.
    bool drawActor(s16 actorId, const std::array<s16, 3>& position,
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
    std::optional<core::SceneSession> pendingSession;
    bool showsFlagLinks = true;
    std::optional<std::pair<actor::SceneFlag, s64>> hoveredFlag;
};

} // namespace toki::scene
