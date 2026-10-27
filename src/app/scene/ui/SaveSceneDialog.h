#pragma once

#include "app/core/ui/Dialog.h"
#include "app/scene/domain/GeometryChanges.h"
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
    /// scene was written to, and `stopReviewing` when the person asks not to
    /// be shown a save again.
    SaveSceneDialog(const workspace::WorkspaceService& workspaceService,
                    std::function<void(const std::filesystem::path&)> saved,
                    std::function<void()> stopReviewing);

    /// Plans saving `opened` with its edits `edited`, or as it was read when
    /// it has none. Unless `isReviewed`, the plan is written without being
    /// shown, and the dialog opens only when it fails.
    void open(const Scene& opened, const SceneModel* edited, bool isReviewed);

    /// Takes how a save that is not shown went; called every frame.
    void update();

    void draw() override;

private:
    void requestPlan(bool isReviewed);
    void finishSave(const std::filesystem::path& root);
    void drawGeometryChanges() const;

    const workspace::WorkspaceService& workspaceService;
    std::function<void(const std::filesystem::path&)> saved;
    std::function<void()> stopReviewing;
    bool isDoNotShowAgain = false;
    bool isSavingUnshown = false;
    std::optional<Scene> scene;
    std::optional<SceneModel> sceneModel;
    GeometryChanges geometryChanges;
    tools::EditFlow editFlow;
};

} // namespace toki::scene
