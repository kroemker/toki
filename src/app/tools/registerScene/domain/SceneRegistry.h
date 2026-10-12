#pragma once

#include "app/tools/Types.h"
#include "hackeroot/Assets.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toki::tools {

/// A scene folder fast64 exported, `assets/scenes/oceantest/`, as registering
/// it reads it: its name, and the C files of its scene and of each room, in
/// the order their segments are linked from, each relative to the checkout.
struct SceneExport {
    std::string name;
    std::filesystem::path directory;
    std::vector<std::filesystem::path> sceneFiles;
    std::vector<std::vector<std::filesystem::path>> rooms;
};

/// How a scene is registered: the draw config its scene table entry names, how
/// many of its spawns get entrances (all it has when not given), whether the
/// map select lists it and as what, and whether the game boots into it, at
/// which spawn, age and time of day, each left as it is when not given.
struct SceneRegistration {
    std::string drawConfig = "SDC_DEFAULT";
    std::optional<usize> spawnCount;
    bool addsMapSelect = true;
    std::string mapSelectLabel;
    bool bootsToScene = false;
    usize bootSpawn = 0;
    std::optional<std::string> bootAge;
    std::optional<std::string> bootTime;
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
[[nodiscard]] std::expected<SurveyedScene, std::string>
surveyScene(const std::shared_ptr<const hackeroot::Workspace>& workspace,
            const hackeroot::AssetDirectory& asset);

/// The map select's name for a scene called `name`: `ocean_test` is
/// "Ocean Test".
[[nodiscard]] std::string makeMapSelectLabel(std::string_view name);

/// The enum names a registered scene is known by: `SCENE_OCEANTEST`, and its
/// entrances, `ENTR_OCEANTEST_0`.
[[nodiscard]] std::string makeSceneEnum(std::string_view name);
[[nodiscard]] std::string makeEntranceEnum(std::string_view name, usize spawn);

/// What registering `scene` as `registration` says does to the checkout at
/// `repository`: a segment in `spec/spec` for the scene and for each room,
/// added or brought up to date with its files; an entry in the scene table;
/// four entrances a spawn, one for each of the layers child day and night,
/// adult day and night, in the entrance table; an entry ahead of the map
/// select's first; and the boot settings of `config_debug.h`, written last so
/// they never name an entrance a failed write left out. What is there already
/// is left alone, so registering a scene again adds only what it gained, as a
/// room.
[[nodiscard]] std::expected<hackeroot::EditPlan, std::string>
makeScenePlan(const std::filesystem::path& repository, const SceneExport& scene,
              const SceneRegistration& registration, const SceneSurvey& survey);

} // namespace toki::tools
