#include "app/tools/registerScene/domain/SceneRegistry.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Assets.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace {

void writeText(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << text;
}

[[nodiscard]] std::string readText(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] std::filesystem::path makeCheckout() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "toki_tests" / "scene_registry";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    writeText(root / "spec/spec", "beginseg\n    name \"ydan_scene\"\nendseg\n\n"
                                  "#if TESTSUITE_MODE\n#include \"t\"\n#endif\n");
    writeText(root / "include/tables/scene_table.h",
              "DEFINE_SCENE(ydan_scene, g_pn_06, SCENE_DEKU_TREE, SDC_DEKU_TREE, 1, 2)\n\n"
              "#if TESTSUITE_MODE\n#endif\n");
    writeText(root / "include/tables/entrance_table.h",
              "DEFINE_ENTRANCE(ENTR_DEKU_TREE_0, SCENE_DEKU_TREE, 0, false, true, "
              "TRANS_TYPE_FADE_BLACK, TRANS_TYPE_FADE_BLACK)\n");
    writeText(root / "src/overlays/gamestates/ovl_select/z_select.h",
              "static SceneSelectEntry sScenes[] = {\n"
              "    { \"Hyrule Field\", MapSelect_LoadGame, ENTR_HYRULE_FIELD_0 },\n};\n");
    writeText(root / "include/config/config_debug.h",
              "#define BOOT_TO_SCENE false\n#define BOOT_ENTRANCE ENTR_HYRULE_FIELD_0\n"
              "#define ENABLE_HACKER_DEBUG true\n");
    return root;
}

[[nodiscard]] toki::registerScene::SceneExport makeExport(const base::usize rooms) {
    toki::registerScene::SceneExport scene{
        .name = "ocean_test",
        .directory = "assets/scenes/ocean_test",
        .sceneFiles = {"assets/scenes/ocean_test/ocean_test_scene_main.c",
                       "assets/scenes/ocean_test/ocean_test_scene_col.c"}};
    for (base::usize room = 0; room < rooms; ++room) {
        scene.rooms.push_back(
            {std::format("assets/scenes/ocean_test/ocean_test_room_{}_main.c", room)});
    }
    return scene;
}

[[nodiscard]] toki::registerScene::SceneSurvey makeSurvey() {
    return toki::registerScene::SceneSurvey{.spawns = 2,
                                            .listedRooms = 1,
                                            .config = {{.name = "BOOT_TO_SCENE",
                                                        .file = "include/config/config_debug.h",
                                                        .line = 1,
                                                        .written = "false"},
                                                       {.name = "BOOT_ENTRANCE",
                                                        .file = "include/config/config_debug.h",
                                                        .line = 2,
                                                        .written = "ENTR_HYRULE_FIELD_0"},
                                                       {.name = "ENABLE_HACKER_DEBUG",
                                                        .file = "include/config/config_debug.h",
                                                        .line = 3,
                                                        .written = "true"}}};
}

} // namespace

TEST_CASE("registering a scene writes every place the build learns of it, then nothing",
          "[tools][register]") {
    const std::filesystem::path root = makeCheckout();
    const toki::registerScene::SceneRegistration registration{
        .mapSelectLabel = toki::registerScene::makeMapSelectLabel("ocean_test"),
        .bootsToScene = true};
    REQUIRE(registration.mapSelectLabel == "Ocean Test");

    const std::expected<hackeroot::EditPlan, std::string> plan =
        toki::registerScene::makeScenePlan(root, makeExport(1), registration, makeSurvey());
    REQUIRE(plan.has_value());
    REQUIRE(plan->warnings.empty());
    REQUIRE(plan->files.size() == 5);
    REQUIRE(plan->files.back().file == std::filesystem::path("include/config/config_debug.h"));
    REQUIRE(hackeroot::applyEditPlan(root, *plan).has_value());

    const std::string spec = readText(root / "spec/spec");
    REQUIRE(spec.contains("    name \"ocean_test_scene\"\n    compress\n    romalign 0x1000\n"
                          "    include \"$(BUILD_DIR)/assets/scenes/ocean_test/"
                          "ocean_test_scene_main.o\"\n"));
    REQUIRE(spec.contains("name \"ocean_test_room_0\""));
    REQUIRE(spec.ends_with("// Toki64 end\n\n#if TESTSUITE_MODE\n#include \"t\"\n#endif\n"));
    REQUIRE(
        readText(root / "include/tables/scene_table.h")
            .contains("DEFINE_SCENE(ocean_test_scene, none, SCENE_OCEAN_TEST, SDC_DEFAULT, 0, 0)"));
    const std::string entrances = readText(root / "include/tables/entrance_table.h");
    REQUIRE(entrances.contains("DEFINE_ENTRANCE(ENTR_OCEAN_TEST_1_3, SCENE_OCEAN_TEST, 1,"));
    REQUIRE(readText(root / "src/overlays/gamestates/ovl_select/z_select.h")
                .contains("    { \"Ocean Test\", MapSelect_LoadGame, ENTR_OCEAN_TEST_0 },\n"
                          "    { \"Hyrule Field\""));
    const std::string config = readText(root / "include/config/config_debug.h");
    REQUIRE(config == "#define BOOT_TO_SCENE true\n#define BOOT_ENTRANCE ENTR_OCEAN_TEST_0\n"
                      "#define ENABLE_HACKER_DEBUG true\n");

    toki::registerScene::SceneSurvey booted = makeSurvey();
    booted.config[0].written = "true";
    booted.config[1].written = "ENTR_OCEAN_TEST_0";
    const std::expected<hackeroot::EditPlan, std::string> again =
        toki::registerScene::makeScenePlan(root, makeExport(1), registration, booted);
    REQUIRE(again.has_value());
    REQUIRE_FALSE(again->hasWork());

    const std::expected<hackeroot::EditPlan, std::string> grown =
        toki::registerScene::makeScenePlan(root, makeExport(2), registration, booted);
    REQUIRE(grown.has_value());
    REQUIRE(grown->hasWork());
    REQUIRE(grown->files.front().items.back().status == hackeroot::EditStatus::Add);
    REQUIRE(grown->files.front().items.back().label == "room segment \"ocean_test_room_1\"");
    REQUIRE(grown->warnings.size() == 1);
    REQUIRE(grown->warnings.front().contains("room list names 1 rooms, but its folder has 2"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("what would break a registration is refused or warned of", "[tools][register]") {
    const std::filesystem::path root = makeCheckout();
    toki::registerScene::SceneSurvey survey = makeSurvey();
    survey.hasDebugFeatures = false;
    survey.scenes = {{.name = "other_scene", .enumName = "SCENE_OCEAN_TEST"}};

    const std::expected<hackeroot::EditPlan, std::string> plan = toki::registerScene::makeScenePlan(
        root, makeExport(1), {.mapSelectLabel = "Ocean", .bootsToScene = true, .bootSpawn = 1},
        survey);
    REQUIRE(plan.has_value());
    REQUIRE(plan->warnings.size() == 2);
    REQUIRE_FALSE(toki::registerScene::makeScenePlan(
                      root, makeExport(1),
                      {.mapSelectLabel = "Ocean", .bootsToScene = true, .bootSpawn = 2}, survey)
                      .has_value());
    const std::expected<hackeroot::EditPlan, std::string> quoted =
        toki::registerScene::makeScenePlan(root, makeExport(1), {.mapSelectLabel = "The \"Ocean\""},
                                           survey);
    REQUIRE(quoted.has_value());
    REQUIRE(quoted->files[3].after.contains(
        "{ \"The \\\"Ocean\\\"\", MapSelect_LoadGame, ENTR_OCEAN_TEST_0 },"));
    REQUIRE_FALSE(
        toki::registerScene::makeScenePlan(root, makeExport(0), {.mapSelectLabel = "Ocean"}, survey)
            .has_value());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("a scene the prototype registered is found registered", "[tools][register][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    const auto& scenes = workspace->getAssets().scenes;
    const auto ocean =
        std::ranges::find(scenes, std::string("oceantest"), &hackeroot::AssetDirectory::name);
    if (ocean == scenes.end()) {
        SKIP("the checkout has no assets/scenes/oceantest");
    }

    const std::expected<toki::registerScene::SurveyedScene, std::string> surveyed =
        toki::registerScene::surveyScene(workspace, *ocean);
    REQUIRE(surveyed.has_value());
    REQUIRE(surveyed->survey.spawns.has_value());
    const std::expected<hackeroot::EditPlan, std::string> plan = toki::registerScene::makeScenePlan(
        repository, surveyed->scene,
        {.mapSelectLabel = toki::registerScene::makeMapSelectLabel("oceantest")}, surveyed->survey);
    REQUIRE(plan.has_value());
    std::string described;
    for (const hackeroot::FileEdit& file : plan->files) {
        for (const hackeroot::EditItem& item : file.items) {
            described += std::format("\n  {} {}: {}", static_cast<int>(item.status),
                                     file.file.generic_string(), item.label);
        }
    }
    for (const std::string& warning : plan->warnings) {
        described += "\n  warning: " + warning;
    }
    WARN("oceantest, " << *surveyed->survey.spawns << " spawns:" << described);
    CHECK_FALSE(plan->hasWork());
}

TEST_CASE("rewriting a scene's segment warns of the lines someone added to it",
          "[tools][register]") {
    const std::filesystem::path root = makeCheckout();
    writeText(root / "spec/spec",
              "beginseg\n    name \"ocean_test_room_0\"\n    compress\n    romalign 0x1000\n"
              "    include \"$(BUILD_DIR)/assets/scenes/ocean_test/ocean_test_room_0_main.o\"\n"
              "    include \"$(BUILD_DIR)/src/mine.o\"\n    number 3\nendseg\n\n"
              "#if TESTSUITE_MODE\n#endif\n");

    const std::expected<hackeroot::EditPlan, std::string> plan = toki::registerScene::makeScenePlan(
        root, makeExport(1), {.mapSelectLabel = "Ocean"}, makeSurvey());

    REQUIRE(plan.has_value());
    REQUIRE(plan->files.front().items[1].status == hackeroot::EditStatus::Update);
    REQUIRE(plan->warnings.size() == 1);
    REQUIRE(plan->warnings.front().contains(
        "rewriting segment \"ocean_test_room_0\" drops what it has now"));
    REQUIRE(plan->warnings.front().contains("include \"$(BUILD_DIR)/src/mine.o\""));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
