#pragma once

#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/ui/Panel.h"
#include "app/tools/registerScene/domain/SceneRegistry.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::registerScene {

/// Makes a scene fast64 exported to `assets/scenes/` part of the build: picks
/// one of the checkout's scene folders, says how it is registered, and writes
/// the plan once it has been reviewed. A scene registered before is listed too,
/// so one that gained a room is brought up to date.
class RegisterScenePanel : public core::Panel {
public:
    RegisterScenePanel(const workspace::WorkspaceService& workspaceService,
                       actor::CollectionCatalog& collectionCatalog);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    [[nodiscard]] bool isTool() const override {
        return true;
    }

    void draw() override;

private:
    void drawScenes(const hackeroot::Workspace& workspace);
    void drawOptions(const hackeroot::Workspace& workspace);
    void select(const hackeroot::AssetDirectory& scene);
    [[nodiscard]] bool drawNames(const char* label, std::string& value,
                                 std::span<const std::string_view> names);
    void drawOptionalNames(const char* label, std::optional<std::string>& value,
                           std::span<const std::string_view> names);

    const workspace::WorkspaceService& workspaceService;
    actor::CollectionCatalog& collectionCatalog;
    std::optional<std::string> selected;
    SceneRegistration registration;
    tools::EditFlow editFlow;
};

} // namespace toki::registerScene
