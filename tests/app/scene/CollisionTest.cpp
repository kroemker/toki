#include "app/scene/domain/Collision.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/SceneFiles.h"
#include "support/SegmentBuilder.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using base::s16;
using base::u16;
using base::u32;
using base::usize;

using testing::SegmentBuilder;

namespace {

constexpr u32 HeaderSize = 0x2C;
constexpr s16 One = 0x7FFF;

void addVector(SegmentBuilder& segment, const std::array<s16, 3> vector) {
    for (const s16 value : vector) {
        segment.addHalf(value);
    }
}

void addPolygon(SegmentBuilder& segment, const u16 type, const std::array<u16, 3> vertices,
                const std::array<s16, 3> normal) {
    segment.addHalf(static_cast<s16>(type));
    for (const u16 vertex : vertices) {
        segment.addHalf(static_cast<s16>(vertex));
    }
    addVector(segment, normal);
    segment.addHalf(0);
}

/// A collision of four vertices, four polygons, two surface types, two
/// cameras and a waterbox, laid out after its header in that order.
struct CollisionFixture {
    z64::MemoryImage image;
    u32 address = 0;
};

[[nodiscard]] CollisionFixture makeFixture(const bool hasListSymbols) {
    constexpr u32 Vertices = HeaderSize;
    constexpr u32 Polygons = Vertices + (4 * 6);
    constexpr u32 Surfaces = Polygons + (4 * 0x10);
    constexpr u32 Cameras = Surfaces + (2 * 8);
    constexpr u32 WaterBoxes = Cameras + (2 * 8);
    constexpr u32 Segment = u32{hackeroot::SceneSegment} << 24;

    SegmentBuilder scene(hackeroot::SceneSegment);
    addVector(scene, {-10, -20, -30});
    addVector(scene, {100, 200, 300});
    scene.addHalf(4);
    scene.addHalf(0);
    scene.addWord(Segment | Vertices);
    scene.addHalf(4);
    scene.addHalf(0);
    scene.addWord(Segment | Polygons);
    scene.addWord(Segment | Surfaces);
    scene.addWord(Segment | Cameras);
    scene.addHalf(1);
    scene.addHalf(0);
    scene.addWord(Segment | WaterBoxes);

    addVector(scene, {0, 0, 0});
    addVector(scene, {0, 0, 100});
    addVector(scene, {100, 0, 0});
    addVector(scene, {0, 100, 0});

    addPolygon(scene, 0, {0, 1, 2}, {0, One, 0});
    addPolygon(scene, 1, {0x2000 | 0, 0x4000 | 3, 1}, {One, 0, 0});
    addPolygon(scene, 5, {0, 9, 2}, {0, One, 0});
    addPolygon(scene, 0, {0, 1, 2}, {0, -One, 0});

    scene.addWord((1U << 0) | (2U << 8) | (5U << 13) | (3U << 21) | (12U << 26) | (1U << 30));
    scene.addWord((9U << 0) | (2U << 4) | (4U << 6) | (7U << 11) | (1U << 17) | (3U << 18) |
                  (32U << 21));
    scene.addWord(0);
    scene.addWord(0);

    scene.addHalf(0x0012);
    scene.addHalf(0);
    scene.addWord(0);
    scene.addHalf(0x0003);
    scene.addHalf(-1);
    scene.addWord(0);

    for (const s16 value : {s16{-500}, s16{-40}, s16{-600}, s16{1000}, s16{1200}, s16{0}}) {
        scene.addHalf(value);
    }
    scene.addWord((1U << 0) | (3U << 8) | (0x3FU << 13));

    CollisionFixture fixture{.image = scene.makeImage(), .address = Segment};
    if (hasListSymbols) {
        fixture.image.symbols.emplace(
            "sceneSurfaceTypes",
            z64::ImageSymbol{.segment = hackeroot::SceneSegment, .offset = Surfaces, .size = 16});
        fixture.image.symbols.emplace(
            "sceneCameras",
            z64::ImageSymbol{.segment = hackeroot::SceneSegment, .offset = Cameras, .size = 16});
    }
    return fixture;
}

} // namespace

TEST_CASE("a collision header gives its vertices, polygons, surfaces, cameras and waterboxes",
          "[scene][collision]") {
    const CollisionFixture fixture = makeFixture(true);

    const toki::scene::Collision collision =
        toki::scene::readCollision(fixture.image, fixture.address);

    REQUIRE(collision.minBounds == std::array<s16, 3>{-10, -20, -30});
    REQUIRE(collision.maxBounds == std::array<s16, 3>{100, 200, 300});
    REQUIRE(collision.vertices.size() == 4);
    REQUIRE(collision.vertices[3] == std::array<s16, 3>{0, 100, 0});
    REQUIRE(collision.polygons.size() == 4);
    REQUIRE(collision.surfaceTypes.size() == 2);
    REQUIRE(collision.cameras == std::vector<toki::scene::CollisionCamera>{
                                     {.setting = 0x12}, {.setting = 3, .count = -1}});
    REQUIRE(collision.waterBoxes.size() == 1);
    REQUIRE(collision.waterBoxes[0].xMin == -500);
    REQUIRE(collision.waterBoxes[0].ySurface == -40);
    REQUIRE(collision.waterBoxes[0].zLength == 1200);

    const toki::scene::CollisionPolygon& wall = collision.polygons[1];
    REQUIRE(toki::scene::getVertexIndex(wall, 0) == 0);
    REQUIRE(toki::scene::getVertexIndex(wall, 1) == 3);
    REQUIRE(toki::scene::getVertexFlags(wall, 0) == 1);
    REQUIRE(toki::scene::getVertexFlags(wall, 1) == 2);
    REQUIRE(toki::scene::findPolygonKind(collision.polygons[0]) == toki::scene::PolygonKind::Floor);
    REQUIRE(toki::scene::findPolygonKind(wall) == toki::scene::PolygonKind::Wall);
    REQUIRE(toki::scene::findPolygonKind(collision.polygons[3]) ==
            toki::scene::PolygonKind::Ceiling);
}

TEST_CASE("a surface type's fields come out of its words as the game takes them",
          "[scene][collision]") {
    const CollisionFixture fixture = makeFixture(true);
    const toki::scene::Collision collision =
        toki::scene::readCollision(fixture.image, fixture.address);

    const toki::scene::SurfaceFields fields =
        toki::scene::readSurfaceFields(collision.surfaceTypes[0]);

    REQUIRE(fields == toki::scene::SurfaceFields{.bgCamIndex = 1,
                                                 .exitIndex = 2,
                                                 .floorType = 5,
                                                 .wallType = 3,
                                                 .floorProperty = 12,
                                                 .isSoft = true,
                                                 .material = 9,
                                                 .floorEffect = 2,
                                                 .lightSetting = 4,
                                                 .echo = 7,
                                                 .canHookshot = true,
                                                 .conveyorSpeed = 3,
                                                 .conveyorDirection = 32});
    const toki::scene::WaterBox& box = collision.waterBoxes[0];
    REQUIRE(toki::scene::getWaterBoxCamera(box) == 1);
    REQUIRE(toki::scene::getWaterBoxLightSetting(box) == 3);
    REQUIRE(toki::scene::getWaterBoxRoom(box) == toki::scene::WaterBoxAllRooms);
}

TEST_CASE("polygons naming what the collision lacks, or with another normal, are named",
          "[scene][collision]") {
    const CollisionFixture fixture = makeFixture(true);

    const toki::scene::Collision collision =
        toki::scene::readCollision(fixture.image, fixture.address);

    REQUIRE(collision.polygons.size() == 4);
    REQUIRE(collision.diagnostics.size() == 3);
    REQUIRE(collision.diagnostics[0].message.find("vertex past the 4") != std::string::npos);
    REQUIRE(collision.diagnostics[0].message.ends_with(": 2"));
    REQUIRE(collision.diagnostics[1].message.find("surface type past the 2") != std::string::npos);
    REQUIRE(collision.diagnostics[1].message.ends_with(": 2"));
    REQUIRE(collision.diagnostics[2].message.find("normal") != std::string::npos);
    REQUIRE(collision.diagnostics[2].message.ends_with(": 3"));
}

TEST_CASE("a list without a symbol is as long as the collision uses", "[scene][collision]") {
    const CollisionFixture fixture = makeFixture(false);

    const toki::scene::Collision collision =
        toki::scene::readCollision(fixture.image, fixture.address);

    REQUIRE(collision.surfaceTypes.size() == 6);
    REQUIRE(collision.diagnostics.front().message.find("surface type list") != std::string::npos);
    REQUIRE(collision.diagnostics.front().message.find("no symbol") != std::string::npos);
}

TEST_CASE("a collision header outside the image is an error, not a failed read",
          "[scene][collision]") {
    const CollisionFixture fixture = makeFixture(true);

    const toki::scene::Collision collision =
        toki::scene::readCollision(fixture.image, fixture.address + 0x1000);

    REQUIRE(collision.polygons.empty());
    REQUIRE(collision.diagnostics.size() == 1);
    REQUIRE(collision.diagnostics[0].severity == base::Severity::Error);
}

TEST_CASE("polygons are counted by their surface type", "[scene][collision]") {
    toki::scene::Collision collision;
    collision.surfaceTypes.resize(3);
    collision.polygons = {{.type = 2}, {.type = 0}, {.type = 2}, {.type = 7}};

    REQUIRE(toki::scene::countSurfacePolygons(collision) == std::vector<usize>{1, 0, 2});
}
