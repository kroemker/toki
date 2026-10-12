#include "SceneSurvey.h"

#include "app/scene/domain/Scene.h"
#include "app/tools/registerScene/Types.h"
#include "cparser/MacroTable.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/SceneFiles.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::registerScene {

namespace {

[[nodiscard]] u8 rankSceneFile(const std::string_view suffix) {
    if (suffix == ".c" || suffix == "_main.c") {
        return 0;
    }
    if (suffix == "_col.c") {
        return 1;
    }
    return suffix.starts_with("_cs") ? 2 : 3;
}

[[nodiscard]] u8 rankRoomFile(const std::string_view suffix) {
    if (suffix == ".c" || suffix == "_main.c") {
        return 0;
    }
    if (suffix == "_model_info.c") {
        return 1;
    }
    return suffix == "_model.c" ? 2 : 3;
}

void orderSegmentFiles(std::vector<std::filesystem::path>& files, const std::string& prefix,
                       const bool isRoom) {
    const auto rank = [&](const std::filesystem::path& file) {
        const std::string name = file.filename().string();
        std::string_view suffix(name);
        suffix.remove_prefix(std::min(prefix.size(), suffix.size()));
        if (isRoom) {
            suffix.remove_prefix(std::min(suffix.find_first_not_of("0123456789"), suffix.size()));
        }
        return std::pair(isRoom ? rankRoomFile(suffix) : rankSceneFile(suffix), name);
    };
    std::ranges::sort(files, {}, rank);
}

} // namespace

std::expected<SurveyedScene, std::string>
surveyScene(const std::shared_ptr<const hackeroot::Workspace>& workspace,
            const hackeroot::AssetDirectory& asset) {
    const hackeroot::SceneFiles files = hackeroot::findSceneFiles(asset);
    if (files.scene.empty() || files.rooms.empty()) {
        return std::unexpected(
            std::format("{} has no scene file and rooms that fast64 names", asset.name));
    }
    const std::filesystem::path& root = workspace->getRoot();
    const auto makeRelative = [&root](const std::filesystem::path& file) {
        return file.lexically_normal().lexically_relative(root.lexically_normal());
    };
    SurveyedScene surveyed;
    surveyed.scene.name = asset.name;
    surveyed.scene.directory = makeRelative(asset.directory);
    std::ranges::transform(files.scene, std::back_inserter(surveyed.scene.sceneFiles),
                           makeRelative);
    orderSegmentFiles(surveyed.scene.sceneFiles, asset.name + "_scene", false);
    for (const std::vector<std::filesystem::path>& room : files.rooms) {
        std::vector<std::filesystem::path>& relative = surveyed.scene.rooms.emplace_back();
        std::ranges::transform(room, std::back_inserter(relative), makeRelative);
        orderSegmentFiles(relative, asset.name + "_room_", true);
    }
    hackeroot::AssetImageReader reader(workspace);
    const scene::Scene read = scene::readScene(reader, asset);
    if (!read.layers.empty()) {
        surveyed.survey.spawns = read.layers.front().header.playerEntries.size();
        surveyed.survey.listedRooms = read.layers.front().rooms.size();
    }
    surveyed.survey.scenes = workspace->getTables().scenes;
    surveyed.survey.config =
        hackeroot::readConfigDefines(root, workspace->getBuildDefines().arguments);
    const cparser::Macro* debug = workspace->getMacros().find("DEBUG_FEATURES");
    surveyed.survey.hasDebugFeatures = debug != nullptr && !debug->body.empty() &&
                                       debug->body.front().text != "0" &&
                                       debug->body.front().text != "false";
    return surveyed;
}

} // namespace toki::registerScene
