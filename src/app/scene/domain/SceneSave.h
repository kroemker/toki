#pragma once

#include "app/scene/domain/SceneModel.h"
#include "hackeroot/EditPlan.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::scene {

/// A scene as it is to be saved: where it was read from, the files it was
/// read from, and its symbols as they are now.
struct SceneSaveInput {
    std::string name;
    std::filesystem::path directory;
    std::vector<std::filesystem::path> files;
    SceneModel model;
};

/// The headers every file of a saved scene is read against, as its
/// `<scene>_scene.h` includes them: fast64's, and what vanilla scenes include
/// beyond them.
[[nodiscard]] const std::vector<std::string>& getSceneIncludes();

/// Where a scene read from `directory` is saved: in the same folder, or for a
/// vanilla scene, read from `extracted/<version>/`, the same path under the
/// checkout's root, which the build compiles in place of the extracted one.
[[nodiscard]] std::filesystem::path findSavedDirectory(const std::filesystem::path& root,
                                                       const std::filesystem::path& directory);

/// Plans writing a scene in fast64's layout over its folder: every file
/// written again from the model's symbols (`writeSceneFiles`), the files of
/// its old layout removed, and the spec's segments of the scene and its rooms
/// made to link the new files. Before anything is planned, the new files are
/// read back from memory as the build would read them and compared with the
/// model symbol by symbol; a scene that does not read back the same is not
/// saved, and neither is one with data under a feature switch that is off.
/// What a save loses is a warning of the plan.
[[nodiscard]] std::expected<hackeroot::EditPlan, std::string>
planSceneSave(const std::shared_ptr<const hackeroot::Workspace>& workspace,
              const SceneSaveInput& input);

} // namespace toki::scene
