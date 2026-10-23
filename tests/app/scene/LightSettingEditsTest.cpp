#include "app/scene/domain/LightSettingEdits.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using base::u8;
using base::usize;

namespace {

z64::ImageObject makeObject(const u8 segment, std::string name, std::string type,
                            std::vector<u8> bytes, std::vector<z64::Relocation> relocations = {}) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = std::move(type),
        .isArray = true,
        .alignment = 4,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

/// A scene whose main header and its layer 1 header share one light setting.
toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(0, "sSetupDL", "u8", {0, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(2, "scene", "SceneCmd", {0x18, 0, 0, 0, 0,    0, 0, 0, 0x0F, 1, 0, 0,
                                             0,    0, 0, 0, 0x14, 0, 0, 0, 0,    0, 0, 0},
                    {{.offset = 4, .symbol = "alternates"}, {.offset = 12, .symbol = "lights"}}),
         makeObject(2, "alternates", "SceneCmd*", {0, 0, 0, 0},
                    {{.offset = 0, .symbol = "sceneAlt"}}),
         makeObject(2, "sceneAlt", "SceneCmd",
                    {0x0F, 1, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "lights"}}),
         makeObject(2, "lights", "EnvLightSettings",
                    {10,   20, 30, 73, 73, 73, 200, 190,  180,  0xB7, 0xB7,
                     0xB7, 40, 50, 60, 70, 80, 90,  0x0B, 0xDE, 0x32, 0x00})},
        {})));
    return scene;
}

toki::scene::SceneHeader readScene(const toki::scene::SceneModel& model, const char* header) {
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();
    return toki::scene::readSceneHeader(*images[1], *images[1]->findAddress(header));
}

constexpr toki::scene::LightSetting Dusk{.ambientColor = {1, 2, 3},
                                         .light1Direction = {-1, 0, 127},
                                         .light1Color = {4, 5, 6},
                                         .light2Direction = {0, -128, 1},
                                         .light2Color = {7, 8, 9},
                                         .fogColor = {10, 11, 12},
                                         .fogNear = 1000,
                                         .blendRate = 63,
                                         .zFar = 6400};

} // namespace

TEST_CASE("a light setting is read back as it was written", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    const toki::scene::LightSetting read = readScene(model, "scene").lightSettings.at(0);
    REQUIRE(read.fogNear == 990);
    REQUIRE(read.blendRate == 2);
    REQUIRE(toki::scene::setLightSetting(model, 0, 0, read));

    REQUIRE(model.findSymbol("scene_header00_lightSettings")->bytes ==
            std::vector<u8>{10,   20, 30, 73, 73, 73, 200, 190,  180,  0xB7, 0xB7,
                            0xB7, 40, 50, 60, 70, 80, 90,  0x0B, 0xDE, 0x32, 0x00});
}

TEST_CASE("a light setting set in one layer leaves the layer it shared it with", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setLightSetting(model, 1, 0, Dusk));

    const std::vector<toki::scene::LightSetting> alternate =
        readScene(model, "sceneAlt").lightSettings;
    REQUIRE(alternate.size() == 1);
    REQUIRE(alternate[0] == Dusk);
    REQUIRE(readScene(model, "scene").lightSettings.at(0).ambientColor ==
            std::array<u8, 3>{10, 20, 30});
    REQUIRE(model.findSymbol("scene_header01_lightSettings") != nullptr);
}

TEST_CASE("a light setting added goes to the end of its layer's list alone", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::addLightSetting(model, 0, Dusk) == usize{1});

    const std::vector<toki::scene::LightSetting> main = readScene(model, "scene").lightSettings;
    REQUIRE(main.size() == 2);
    REQUIRE(main[1] == Dusk);
    REQUIRE(readScene(model, "sceneAlt").lightSettings.size() == 1);
}

TEST_CASE("a light setting removed leaves the others, but not none", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());
    REQUIRE(toki::scene::addLightSetting(model, 0, Dusk));

    REQUIRE(toki::scene::removeLightSetting(model, 0, 0));

    const std::vector<toki::scene::LightSetting> main = readScene(model, "scene").lightSettings;
    REQUIRE(main.size() == 1);
    REQUIRE(main[0] == Dusk);
    REQUIRE_FALSE(toki::scene::removeLightSetting(model, 0, 0));
}
