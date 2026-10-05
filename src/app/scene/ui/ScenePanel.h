#pragma once

#include "app/core/ui/Panel.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <array>
#include <optional>
#include <string>

namespace toki::scene {

/// The open scene: which of its layers and light settings the viewport shows,
/// which rooms, and what is placed in them.
class ScenePanel : public core::Panel {
public:
    ScenePanel(const workspace::WorkspaceService& workspaceService,
               viewport::ViewportPanel& viewportPanel);

    /// Shows `scene` in the viewport. A scene read again keeps how it was
    /// shown and where the camera is; another one starts afresh at its first
    /// entrance.
    void setScene(Scene read);

    void draw() override;

private:
    void showScene();
    void drawPickers(const Scene& shown);
    void drawRooms(const SceneLayer& layer);
    void drawPlacements(const SceneLayer& layer);
    void drawActor(s16 actorId, const std::array<s16, 3>& position, const char* label);
    [[nodiscard]] std::string findActorName(s16 actorId) const;

    const workspace::WorkspaceService& workspaceService;
    viewport::ViewportPanel& viewportPanel;
    std::optional<Scene> scene;
    SceneView sceneView;
};

} // namespace toki::scene
