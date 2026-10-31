#include "app/scene/domain/PathEdits.h"

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

using base::s16;
using base::u8;
using base::usize;

namespace {

using Points = std::vector<std::array<s16, 3>>;

z64::ImageObject makeObject(const u8 segment, std::string name, std::string type,
                            std::vector<u8> bytes, std::vector<z64::Relocation> relocations = {},
                            const usize alignment = 4) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = std::move(type),
        .isArray = true,
        .alignment = alignment,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

/// A scene whose main header and its layer 1 header share one path list of a
/// path of two points.
toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(0, "sSetupDL", "u8", {0, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(2, "scene", "SceneCmd", {0x18, 0, 0, 0, 0,    0, 0, 0, 0x0D, 0, 0, 0,
                                             0,    0, 0, 0, 0x14, 0, 0, 0, 0,    0, 0, 0},
                    {{.offset = 4, .symbol = "alternates"}, {.offset = 12, .symbol = "paths"}}),
         makeObject(2, "alternates", "SceneCmd*", {0, 0, 0, 0},
                    {{.offset = 0, .symbol = "sceneAlt"}}),
         makeObject(2, "sceneAlt", "SceneCmd",
                    {0x0D, 0, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "paths"}}),
         makeObject(2, "points", "Vec3s", {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6}, {}, 2),
         makeObject(2, "paths", "Path", {2, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "points"}})},
        {})));
    return scene;
}

toki::scene::SceneHeader readScene(const toki::scene::SceneModel& model, const char* header) {
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();
    return toki::scene::readSceneHeader(*images[1], *images[1]->findAddress(header));
}

} // namespace

TEST_CASE("a path's points set in one layer leave the layer it shared them with", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());
    const Points moved{{10, 20, 30}, {40, 50, 60}, {-70, -80, -90}};

    REQUIRE(toki::scene::setPathPoints(model, 1, 0, moved));

    const toki::scene::SceneHeader alternate = readScene(model, "sceneAlt");
    REQUIRE(alternate.diagnostics.empty());
    REQUIRE(alternate.paths.size() == 1);
    REQUIRE(alternate.paths[0].points == moved);
    REQUIRE(readScene(model, "scene").paths.at(0).points == Points{{1, 2, 3}, {4, 5, 6}});
    REQUIRE(model.findSymbol("scene_header01_pathList") != nullptr);
    REQUIRE(model.findSymbol("scene_header01_pathList_points0") != nullptr);
}

TEST_CASE("a path keeps at least one point, and at most as many as its count holds",
          "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE_FALSE(toki::scene::setPathPoints(model, 0, 0, Points{}));
    REQUIRE_FALSE(toki::scene::setPathPoints(model, 0, 0, Points(256)));
    REQUIRE_FALSE(toki::scene::setPathPoints(model, 0, 1, Points{{0, 0, 0}}));
}

TEST_CASE("a path added goes to the end of its layer's list alone", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::addPath(model, 0, Points{{7, 8, 9}}) == usize{1});

    const toki::scene::SceneHeader main = readScene(model, "scene");
    REQUIRE(main.paths.size() == 2);
    REQUIRE(main.paths[0].points == Points{{1, 2, 3}, {4, 5, 6}});
    REQUIRE(main.paths[1].points == Points{{7, 8, 9}});
    REQUIRE(readScene(model, "sceneAlt").paths.size() == 1);
}

TEST_CASE("a header without paths is given a path list when one is added", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());
    REQUIRE(toki::scene::removePath(model, 0, 0));
    REQUIRE(readScene(model, "scene").paths.empty());

    REQUIRE(toki::scene::addPath(model, 0, Points{{7, 8, 9}}) == usize{0});

    REQUIRE(readScene(model, "scene").paths.at(0).points == Points{{7, 8, 9}});
}

TEST_CASE("the last path removed takes its command and its list with it", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::removePath(model, 0, 0));

    const toki::scene::SceneHeader main = readScene(model, "scene");
    REQUIRE(main.paths.empty());
    REQUIRE(main.diagnostics.empty());
    REQUIRE(model.findSymbol("scene_header00_pathList") == nullptr);
    REQUIRE(readScene(model, "sceneAlt").paths.size() == 1);
    REQUIRE(model.findSymbol("points") != nullptr);
}

TEST_CASE("a path removed takes the points nothing else points at", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());
    REQUIRE(toki::scene::addPath(model, 1, Points{{7, 8, 9}}));

    REQUIRE(toki::scene::removePath(model, 1, 1));

    REQUIRE(readScene(model, "sceneAlt").paths.size() == 1);
    REQUIRE(model.findSymbol("scene_header01_pathList_points1") == nullptr);
}
