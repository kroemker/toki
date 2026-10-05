#include "hackeroot/SceneFiles.h"

#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

/// A scene folder in the temp directory holding empty files of the given names.
class SceneFolder {
public:
    explicit SceneFolder(const std::vector<std::string>& names)
        : directory(std::filesystem::temp_directory_path() / "toki_tests" / "scene_files" /
                    "village") {
        std::error_code failure;
        std::filesystem::remove_all(directory, failure);
        std::filesystem::create_directories(directory, failure);
        for (const std::string& name : names) {
            std::ofstream(directory / name) << "";
        }
    }

    SceneFolder(const SceneFolder&) = delete;
    SceneFolder(SceneFolder&&) = delete;
    SceneFolder& operator=(const SceneFolder&) = delete;
    SceneFolder& operator=(SceneFolder&&) = delete;

    ~SceneFolder() {
        std::error_code failure;
        std::filesystem::remove_all(directory, failure);
    }

    [[nodiscard]] hackeroot::AssetDirectory makeAsset() const {
        return hackeroot::AssetDirectory{.name = "village", .directory = directory};
    }

    [[nodiscard]] static std::vector<std::string>
    makeNames(const std::vector<std::filesystem::path>& paths) {
        std::vector<std::string> names;
        names.reserve(paths.size());
        for (const std::filesystem::path& path : paths) {
            names.push_back(path.filename().string());
        }
        return names;
    }

private:
    std::filesystem::path directory;
};

} // namespace

TEST_CASE("a fast64 scene's files group by segment, the header's file first",
          "[hackeroot][scenes]") {
    const SceneFolder folder({"village_scene_col.c", "village_scene_main.c", "village_scene.h",
                              "village_room_1_main.c", "village_room_0_model.c",
                              "village_room_0_main.c", "village_room_0_model_info.c",
                              "village_room_0.inc.c", "notes.txt"});

    const hackeroot::SceneFiles files = hackeroot::findSceneFiles(folder.makeAsset());

    REQUIRE(files.diagnostics.empty());
    REQUIRE(SceneFolder::makeNames(files.scene) ==
            std::vector<std::string>{"village_scene_main.c", "village_scene_col.c"});
    REQUIRE(files.rooms.size() == 2);
    REQUIRE(SceneFolder::makeNames(files.rooms[0]) ==
            std::vector<std::string>{"village_room_0_main.c", "village_room_0_model.c",
                                     "village_room_0_model_info.c"});
    REQUIRE(SceneFolder::makeNames(files.rooms[1]) ==
            std::vector<std::string>{"village_room_1_main.c"});
    REQUIRE(SceneFolder::makeNames(files.headers) == std::vector<std::string>{"village_scene.h"});
    REQUIRE(files.getAllFiles().size() == 7);

    const std::vector<std::vector<hackeroot::SegmentFiles>> images = files.makeImageFiles();
    REQUIRE(images.size() == 3);
    REQUIRE(images[0].front().segment == hackeroot::SceneSegment);
    REQUIRE(images[2].front().segment == hackeroot::RoomSegment);
}

TEST_CASE("rooms after a missing one are not read, and that is said", "[hackeroot][scenes]") {
    const SceneFolder folder({"village_scene.c", "village_room_0.c", "village_room_2.c"});

    const hackeroot::SceneFiles files = hackeroot::findSceneFiles(folder.makeAsset());

    REQUIRE(files.rooms.size() == 1);
    REQUIRE(files.diagnostics.size() == 1);
    REQUIRE(files.diagnostics.front().message.contains("room 1"));
}

TEST_CASE("a folder without the scene's file says so", "[hackeroot][scenes]") {
    const SceneFolder folder({"village_room_0.c"});

    const hackeroot::SceneFiles files = hackeroot::findSceneFiles(folder.makeAsset());

    REQUIRE(files.scene.empty());
    REQUIRE(files.diagnostics.size() == 1);
}
