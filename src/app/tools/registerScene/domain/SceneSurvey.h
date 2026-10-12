#pragma once

#include "app/tools/registerScene/Types.h"
#include "hackeroot/Assets.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toki::registerScene {

/// A scene folder fast64 exported, `assets/scenes/oceantest/`, as registering
/// it reads it: its name, and the C files of its scene and of each room, in
/// the order their segments are linked from, each relative to the checkout.
struct SceneExport {
    std::string name;
    std::filesystem::path directory;
    std::vector<std::filesystem::path> sceneFiles;
    std::vector<std::vector<std::filesystem::path>> rooms;
};

/// What a registration is planned from, read from the checkout beforehand: the
/// spawns the scene's header lists and the rooms its room list names, when the
/// scene could be read, the scene table, the configuration's settings, and
/// whether the build has `DEBUG_FEATURES`, without which the safeguards keep it
/// from booting into a scene.
struct SceneSurvey {
    std::optional<usize> spawns;
    std::optional<usize> listedRooms;
    std::vector<hackeroot::SceneEntry> scenes;
    std::vector<hackeroot::ConfigDefine> config;
    bool hasDebugFeatures = true;
};

/// A scene to register as read from the checkout, and what it is planned from.
struct SurveyedScene {
    SceneExport scene;
    SceneSurvey survey;
};

/// Reads the scene folder `asset` of `workspace`'s checkout for registering
/// it: its files (`hackeroot::findSceneFiles`), its spawns and room list out of
/// the scene as the game loads it, and the tables and configuration. A scene
/// that does not read still has its files; only its spawns are not known.
/// The workspace is shared, as the image reader the scene is read with keeps
/// it for as long as it reads.
[[nodiscard]] std::expected<SurveyedScene, std::string>
surveyScene(const std::shared_ptr<const hackeroot::Workspace>& workspace,
            const hackeroot::AssetDirectory& asset);

} // namespace toki::registerScene
