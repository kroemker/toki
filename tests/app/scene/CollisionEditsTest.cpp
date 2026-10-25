#include "app/scene/domain/CollisionEdits.h"

#include "app/scene/domain/Collision.h"
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

using base::u16;
using base::u32;
using base::u8;
using base::usize;

namespace {

z64::ImageObject makeObject(std::string name, std::string type, std::vector<u8> bytes,
                            std::vector<z64::Relocation> relocations = {},
                            const bool isArray = true, const u8 segment = 2) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = std::move(type),
        .isArray = isArray,
        .alignment = 4,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

std::vector<u8> makeCollisionHeader() {
    std::vector<u8> bytes(0x2C, 0);
    bytes[0x0D] = 3;
    bytes[0x15] = 2;
    bytes[0x25] = 1;
    return bytes;
}

/// A scene whose main header and its layer 1 header point at one collision
/// header: three vertices, two polygons of surface types 0 and 2 (1 is not
/// used), two cameras and a waterbox.
toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("sSetupDL", "u8", std::vector<u8>(8, 0), {}, true, 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject("scene", "SceneCmd", {0x18, 0, 0, 0, 0,    0, 0, 0, 0x03, 0, 0, 0,
                                          0,    0, 0, 0, 0x14, 0, 0, 0, 0,    0, 0, 0},
                    {{.offset = 4, .symbol = "alternates"},
                     {.offset = 12, .symbol = "scene_collisionHeader"}}),
         makeObject("alternates", "SceneCmd*", {0, 0, 0, 0}, {{.offset = 0, .symbol = "sceneAlt"}}),
         makeObject("sceneAlt", "SceneCmd", {0x03, 0, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "scene_collisionHeader"}}),
         makeObject("scene_vertices", "Vec3s",
                    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x64, 0, 0x64, 0, 0, 0, 0}),
         makeObject("scene_polygons", "CollisionPoly",
                    {0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0x7F, 0xFF, 0, 0, 0, 0,
                     0, 2, 0, 0, 0, 1, 0, 2, 0, 0, 0x7F, 0xFF, 0, 0, 0, 0}),
         makeObject("scene_polygonTypes", "SurfaceType",
                    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 3}),
         makeObject("scene_bgCamInfo", "BgCamInfo",
                    {0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0}),
         makeObject("scene_waterBoxes", "WaterBox",
                    {0, 0, 0, 0, 0, 0, 0, 0x64, 0, 0x64, 0, 0, 0, 0, 0, 0}),
         makeObject("scene_collisionHeader", "CollisionHeader", makeCollisionHeader(),
                    {{.offset = 0x10, .symbol = "scene_vertices"},
                     {.offset = 0x18, .symbol = "scene_polygons"},
                     {.offset = 0x1C, .symbol = "scene_polygonTypes"},
                     {.offset = 0x20, .symbol = "scene_bgCamInfo"},
                     {.offset = 0x28, .symbol = "scene_waterBoxes"}},
                    false)},
        {})));
    return scene;
}

toki::scene::Collision readLayerCollision(const toki::scene::SceneModel& model,
                                          const char* header) {
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();
    const toki::scene::SceneHeader read =
        toki::scene::readSceneHeader(*images[1], *images[1]->findAddress(header));
    return toki::scene::readCollision(*images[1], read.collisionHeader);
}

} // namespace

TEST_CASE("a collision edit of one layer shows in every layer that reads the same header",
          "[scene][edit][collision]") {
    toki::scene::SceneModel model(makeScene());
    const std::array<usize, 2> both{0, 1};

    REQUIRE(toki::scene::setSurfaceType(model, 1, 1, {.data = {0x1234, 0x5678}}));
    REQUIRE(toki::scene::setPolygonSurfaceType(model, 1, both, 1));
    REQUIRE(
        toki::scene::setPolygonFlag(model, 1, both, toki::scene::PolygonFlag::IgnoreEntity, true));
    REQUIRE(toki::scene::setPolygonFlag(model, 1, both, toki::scene::PolygonFlag::Conveyor, true));
    REQUIRE(toki::scene::setCameraSetting(model, 1, 1, 7));

    for (const char* header : {"scene", "sceneAlt"}) {
        const toki::scene::Collision collision = readLayerCollision(model, header);
        REQUIRE(collision.diagnostics.empty());
        REQUIRE(collision.surfaceTypes[1] == toki::scene::SurfaceType{.data = {0x1234, 0x5678}});
        for (const toki::scene::CollisionPolygon& polygon : collision.polygons) {
            REQUIRE(polygon.type == 1);
            REQUIRE(toki::scene::hasPolygonFlag(polygon, toki::scene::PolygonFlag::IgnoreEntity));
            REQUIRE(toki::scene::hasPolygonFlag(polygon, toki::scene::PolygonFlag::Conveyor));
            REQUIRE_FALSE(
                toki::scene::hasPolygonFlag(polygon, toki::scene::PolygonFlag::IgnoreCamera));
        }
        REQUIRE(collision.cameras[1].setting == 7);
    }
    REQUIRE(readLayerCollision(model, "scene").polygons[1].vertexData ==
            std::array<u16, 3>{0x4000, 0x2001, 2});
    REQUIRE_FALSE(toki::scene::setPolygonSurfaceType(model, 0, both, 3));
}

TEST_CASE("surface types no polygon has are removed, the polygons' indices moved down",
          "[scene][edit][collision]") {
    toki::scene::SceneModel model(makeScene());

    const auto removed = toki::scene::removeUnusedSurfaceTypes(model, 0);

    REQUIRE(removed == 1);
    const toki::scene::Collision collision = readLayerCollision(model, "scene");
    REQUIRE(collision.surfaceTypes.size() == 2);
    REQUIRE(collision.surfaceTypes[1].data[1] == 3);
    REQUIRE(collision.polygons[1].type == 1);
    REQUIRE(toki::scene::removeUnusedSurfaceTypes(model, 0) == 0);

    REQUIRE(toki::scene::addSurfaceType(model, 0, {.data = {0, 9}}) == 2);
    REQUIRE(readLayerCollision(model, "scene").surfaceTypes.at(2).data[1] == 9);
}

TEST_CASE("waterboxes are set, added and removed, the list going with the last of them",
          "[scene][edit][collision]") {
    toki::scene::SceneModel model(makeScene());
    const toki::scene::WaterBox box{.xMin = -50,
                                    .ySurface = 20,
                                    .zMin = 30,
                                    .xLength = 200,
                                    .zLength = 300,
                                    .properties = toki::scene::makeWaterBoxProperties(
                                        1, 2, toki::scene::WaterBoxAllRooms, true)};

    REQUIRE(toki::scene::setWaterBox(model, 0, 0, box));
    REQUIRE(toki::scene::addWaterBox(model, 0, box) == 1);
    toki::scene::Collision collision = readLayerCollision(model, "sceneAlt");
    REQUIRE(collision.waterBoxes == std::vector<toki::scene::WaterBox>{box, box});
    REQUIRE(toki::scene::getWaterBoxRoom(box) == toki::scene::WaterBoxAllRooms);
    REQUIRE(toki::scene::hasWaterBoxFlag19(box));

    REQUIRE(toki::scene::removeWaterBox(model, 0, 0));
    REQUIRE(readLayerCollision(model, "scene").waterBoxes.size() == 1);
    REQUIRE(toki::scene::removeWaterBox(model, 0, 0));
    REQUIRE(readLayerCollision(model, "scene").waterBoxes.empty());
    REQUIRE(model.findSymbol("scene_waterBoxes") == nullptr);
    REQUIRE_FALSE(toki::scene::removeWaterBox(model, 0, 0));

    REQUIRE(toki::scene::addWaterBox(model, 0, box) == 0);
    REQUIRE(model.findSymbol("scene_waterBoxes") != nullptr);
    REQUIRE(readLayerCollision(model, "scene").waterBoxes ==
            std::vector<toki::scene::WaterBox>{box});
}

TEST_CASE("a layer given its own collision is edited without the layer it shared it with",
          "[scene][edit][collision]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::makeOwnCollision(model, 1));
    REQUIRE(toki::scene::setSurfaceType(model, 1, 0, {.data = {0, 0xAB}}));
    REQUIRE(toki::scene::removeWaterBox(model, 1, 0));

    const toki::scene::Collision own = readLayerCollision(model, "sceneAlt");
    REQUIRE(own.diagnostics.empty());
    REQUIRE(own.surfaceTypes[0].data[1] == 0xAB);
    REQUIRE(own.waterBoxes.empty());
    REQUIRE(own.polygons.size() == 2);
    const toki::scene::Collision shared = readLayerCollision(model, "scene");
    REQUIRE(shared.surfaceTypes[0].data[1] == 1);
    REQUIRE(shared.waterBoxes.size() == 1);
    REQUIRE(model.findSymbol("scene_header01_collisionHeader") != nullptr);
    REQUIRE(model.findSymbol("scene_header01_polygons") != nullptr);

    const usize symbols = model.getImages().front().size();
    REQUIRE(toki::scene::makeOwnCollision(model, 1));
    REQUIRE(model.getImages().front().size() == symbols);
}

TEST_CASE("a surface type's fields pack back into its words, keeping the bits none holds",
          "[scene][collision]") {
    const toki::scene::SurfaceType surface{.data = {0xFFFF'FFFF, 0xFFFF'FFFF}};

    REQUIRE(toki::scene::makeSurfaceType(toki::scene::readSurfaceFields(surface), surface) ==
            surface);
    const toki::scene::SurfaceType made = toki::scene::makeSurfaceType(
        {.exitIndex = 3, .floorType = 5, .material = 2, .canHookshot = true});
    REQUIRE(made.data == std::array<u32, 2>{(3U << 8) | (5U << 13), 2U | (1U << 17)});
}
