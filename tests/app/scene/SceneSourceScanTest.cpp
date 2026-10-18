#include "app/scene/domain/SceneSourceScan.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

namespace {

bool isAnimatedMaterialsOn(const std::string_view name) {
    return name == "ENABLE_ANIMATED_MATERIALS";
}

} // namespace

TEST_CASE("include guards, pragmas and lengths are not lost in a save", "[scene][save]") {
    const std::vector<toki::scene::SceneSource> sources{
        {.file = "a_scene.h",
         .text = "#ifndef A_SCENE_H\n#define A_SCENE_H\n#include \"ultra64.h\"\n"
                 "#define LENGTH_a_actors 3\nextern ActorEntry a_actors[LENGTH_a_actors];\n"
                 "#endif\n"},
        {.file = "a_room_0.h", .text = "#pragma once\nextern Gfx a_dl[];\n"},
    };

    const toki::scene::SceneSourceScan scan =
        toki::scene::scanSceneSources(sources, isAnimatedMaterialsOn);

    REQUIRE(scan.warnings.empty());
    REQUIRE(scan.refusals.empty());
    REQUIRE(scan.lengthDefines.contains("a_actors"));
    REQUIRE_FALSE(scan.isTokiScene);
}

TEST_CASE("other defines and conditions are lost, and data under a switch that is off refuses",
          "[scene][save]") {
    const std::vector<toki::scene::SceneSource> sources{
        {.file = "a_scene_main.c",
         .text = "// toki: scene\n#define SCALE 2\n#ifdef DEBUG\nint x;\n#else\nint y;\n#endif\n"
                 "#if ENABLE_ANIMATED_MATERIALS\nint m;\n#endif\n"
                 "#if ENABLE_MM_TITLE_CARDS\nint t;\n#endif\n"},
    };

    const toki::scene::SceneSourceScan scan =
        toki::scene::scanSceneSources(sources, isAnimatedMaterialsOn);

    REQUIRE(scan.isTokiScene);
    REQUIRE(scan.defines.size() == 1);
    REQUIRE(scan.defines.front().name == "SCALE");
    REQUIRE(scan.defines.front().line == "#define SCALE 2");
    REQUIRE(scan.warnings.size() == 1);
    REQUIRE(scan.warnings.front().contains("#ifdef DEBUG, #else"));
    REQUIRE(scan.refusals.size() == 1);
    REQUIRE(scan.refusals.front().contains("ENABLE_MM_TITLE_CARDS"));
}
