#pragma once

#include "app/core/ui/Dialog.h"
#include "app/scene/domain/Scene.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <optional>

namespace toki::scene {

/// Saves the open scene over its files, or for a vanilla scene to a copy in
/// `assets/`: the plan is made as soon as it is opened, and written once it
/// has been reviewed.
class SaveSceneDialog : public core::Dialog {
public:
    explicit SaveSceneDialog(const workspace::WorkspaceService& workspaceService);

    /// Plans saving `opened` as it is now.
    void open(const Scene& opened);

    void draw() override;

private:
    void requestPlan();

    const workspace::WorkspaceService& workspaceService;
    std::optional<Scene> scene;
    tools::EditFlow editFlow;
};

} // namespace toki::scene
