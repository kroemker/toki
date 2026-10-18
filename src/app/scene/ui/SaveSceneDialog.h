#pragma once

#include "app/core/ui/Dialog.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace toki::scene {

/// Saves the open scene over its files, or for a vanilla scene to a copy in
/// `assets/`: the plan is made as soon as it is opened, and written once it
/// has been reviewed.
class SaveSceneDialog : public core::Dialog {
public:
    /// `saved` is called once the files are written, with the folder the
    /// scene was written to.
    SaveSceneDialog(const workspace::WorkspaceService& workspaceService,
                    std::function<void(const std::filesystem::path&)> saved);

    /// Plans saving `opened` with its edits `edited`, or as it was read when
    /// it has none.
    void open(const Scene& opened, const SceneModel* edited);

    void draw() override;

private:
    void requestPlan();

    const workspace::WorkspaceService& workspaceService;
    std::function<void(const std::filesystem::path&)> saved;
    std::optional<Scene> scene;
    std::optional<SceneModel> sceneModel;
    tools::EditFlow editFlow;
};

} // namespace toki::scene
