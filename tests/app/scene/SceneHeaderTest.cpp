#include "app/scene/domain/SceneHeader.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/SceneFiles.h"
#include "support/SegmentBuilder.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using base::s16;
using base::s8;
using base::u16;
using base::u32;
using base::u8;
using base::usize;

using testing::SegmentBuilder;

TEST_CASE("a scene header gives its rooms, entries, light settings and light mode",
          "[scene][header]") {
    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x04, 2, 0);
    scene.addCommand(0x00, 1, 0);
    scene.addCommand(0x0F, 1, 0);
    scene.addCommand(0x11, 0, 0x1D'00'01'00);
    scene.addCommand(0x14, 0, 0);
    scene.setWord(12, scene.getAddress());
    for (const s16 half :
         {s16{0}, s16{-100}, s16{20}, s16{300}, s16{0}, s16{0x4000}, s16{0}, s16{0x0FFF}}) {
        scene.addHalf(half);
    }
    scene.setWord(20, scene.getAddress());
    scene.addBytes(
        {10, 20, 30, 73, 73, 73, 200, 190, 180, 0xB7, 0xB7, 0xB7, 40, 50, 60, 70, 80, 90});
    scene.addHalf(static_cast<s16>((2 << 10) | 990));
    scene.addHalf(12800);

    const toki::scene::SceneHeader header = toki::scene::readSceneHeader(scene.makeImage());

    REQUIRE(header.diagnostics.empty());
    REQUIRE(header.roomCount == 2);
    REQUIRE(header.skyboxId == 0x1D);
    REQUIRE(header.lightMode == toki::scene::LightMode::Settings);
    REQUIRE(header.playerEntries.size() == 1);
    REQUIRE(header.playerEntries[0].position == std::array<s16, 3>{-100, 20, 300});
    REQUIRE(header.playerEntries[0].rotation[1] == 0x4000);
    REQUIRE(header.playerEntries[0].params == 0x0FFF);
    REQUIRE(header.lightSettings.size() == 1);
    REQUIRE(header.lightSettings[0].ambientColor == std::array<u8, 3>{10, 20, 30});
    REQUIRE(header.lightSettings[0].light2Direction == std::array<s8, 3>{-73, -73, -73});
    REQUIRE(header.lightSettings[0].fogColor == std::array<u8, 3>{70, 80, 90});
    REQUIRE(header.lightSettings[0].fogNear == 990);
    REQUIRE(header.lightSettings[0].blendRate == 2);
    REQUIRE(header.lightSettings[0].zFar == 12800);
}

TEST_CASE("a cullable room gives each entry's display lists", "[scene][header]") {
    SegmentBuilder room(hackeroot::RoomSegment);
    room.addCommand(0x0A, 0, 0);
    room.addCommand(0x14, 0, 0);
    room.setWord(4, room.getAddress());
    room.addBytes({2, 2, 0, 0});
    room.addWord(room.getAddress() + 8);
    room.addWord(0);
    for (const u32 lists : {0x0300'1000U, 0x0300'2000U}) {
        for (const s16 half : {s16{1}, s16{2}, s16{3}, s16{500}}) {
            room.addHalf(half);
        }
        room.addWord(lists);
        room.addWord(lists == 0x0300'1000U ? 0 : 0x0300'3000U);
    }

    const toki::scene::RoomHeader header = toki::scene::readRoomHeader(room.makeImage());

    REQUIRE(header.diagnostics.empty());
    REQUIRE(header.shapeType == toki::scene::RoomShapeType::Cullable);
    REQUIRE(header.displayLists.size() == 2);
    REQUIRE(header.displayLists[0].opaque == 0x0300'1000U);
    REQUIRE(header.displayLists[0].translucent == 0);
    REQUIRE(header.displayLists[1].translucent == 0x0300'3000U);
}

TEST_CASE("a header that runs off its segment says so", "[scene][header]") {
    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x04, 1, 0);

    const toki::scene::SceneHeader header = toki::scene::readSceneHeader(scene.makeImage());

    REQUIRE(header.roomCount == 1);
    REQUIRE(header.diagnostics.size() == 1);
    REQUIRE(header.diagnostics[0].message.contains("without SCENE_CMD_END"));
}

TEST_CASE("a room without a shape has nothing to draw", "[scene][header]") {
    SegmentBuilder room(hackeroot::RoomSegment);
    room.addCommand(0x14, 0, 0);

    const toki::scene::RoomHeader header = toki::scene::readRoomHeader(room.makeImage());

    REQUIRE(header.displayLists.empty());
    REQUIRE(header.diagnostics.size() == 1);
    REQUIRE(header.diagnostics[0].severity == base::Severity::Warning);
}

TEST_CASE("an alternate header list is as long as the array it points at", "[scene][header]") {
    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x18, 0, 0x0200'0010);
    scene.addCommand(0x14, 0, 0);
    scene.addWord(0);
    scene.addWord(0x0200'0020);
    scene.addWord(0);
    scene.addWord(0);
    scene.addCommand(0x0E, 1, 0x0200'0030);
    scene.addCommand(0x14, 0, 0);
    scene.addBytes({1, 0xFF, 0xFE, 0xFF});
    for (const s16 half : {s16{0x0009}, s16{10}, s16{-20}, s16{30}, s16{0x4000}, s16{0x003F}}) {
        scene.addHalf(half);
    }
    z64::MemoryImage image = scene.makeImage();
    image.symbols.emplace(
        "sceneAlternateHeaders",
        z64::ImageSymbol{.segment = hackeroot::SceneSegment, .offset = 0x10, .size = 8});

    const toki::scene::SceneHeader main = toki::scene::readSceneHeader(image);
    REQUIRE(main.diagnostics.empty());
    REQUIRE(main.alternateHeaders == std::vector<u32>{0, 0x0200'0020});

    const toki::scene::SceneHeader alternate =
        toki::scene::readSceneHeader(image, main.alternateHeaders[1]);
    REQUIRE(alternate.diagnostics.empty());
    REQUIRE(alternate.alternateHeaders.empty());
    REQUIRE(alternate.transitionActors.size() == 1);
    REQUIRE(alternate.transitionActors[0].rooms == std::array<s8, 2>{1, -2});
    REQUIRE(alternate.transitionActors[0].id == 0x0009);
    REQUIRE(alternate.transitionActors[0].position == std::array<s16, 3>{10, -20, 30});
    REQUIRE(alternate.transitionActors[0].rotationY == 0x4000);
    REQUIRE(alternate.transitionActors[0].params == 0x003F);
}

TEST_CASE("a room lists the objects it loads, and a scene its keep object", "[scene][header]") {
    SegmentBuilder room(hackeroot::RoomSegment);
    room.addCommand(0x0A, 0, 0);
    room.addCommand(0x0B, 3, 0);
    room.addCommand(0x14, 0, 0);
    room.setWord(12, room.getAddress());
    for (const s16 id : {s16{0x0015}, s16{0x00C8}, s16{0x0170}}) {
        room.addHalf(id);
    }
    room.addHalf(0);
    room.setWord(4, room.getAddress());
    room.addBytes({0, 0, 0, 0});
    room.addWord(0);

    const toki::scene::RoomHeader roomHeader = toki::scene::readRoomHeader(room.makeImage());

    REQUIRE(roomHeader.diagnostics.empty());
    REQUIRE(roomHeader.objects == std::vector<s16>{0x0015, 0x00C8, 0x0170});

    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x07, 2, 0x0003);
    scene.addCommand(0x14, 0, 0);

    const toki::scene::SceneHeader sceneHeader = toki::scene::readSceneHeader(scene.makeImage());

    REQUIRE(sceneHeader.keepObjectId == 3);
}

TEST_CASE("a path list is as long as the array it points at, each path its own count",
          "[scene][header]") {
    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x0D, 0, 0x0200'0010);
    scene.addCommand(0x14, 0, 0);
    scene.addBytes({2, 0, 0, 0});
    scene.addWord(0x0200'0020);
    scene.addBytes({1, 0, 0, 0});
    scene.addWord(0x0200'002C);
    for (const s16 half :
         {s16{1}, s16{2}, s16{3}, s16{-4}, s16{-5}, s16{-6}, s16{70}, s16{80}, s16{90}}) {
        scene.addHalf(half);
    }
    z64::MemoryImage image = scene.makeImage();
    image.symbols.emplace(
        "scenePaths",
        z64::ImageSymbol{.segment = hackeroot::SceneSegment, .offset = 0x10, .size = 0x10});

    const toki::scene::SceneHeader header = toki::scene::readSceneHeader(image);

    REQUIRE(header.diagnostics.empty());
    REQUIRE(header.paths.size() == 2);
    REQUIRE(header.paths[0].points == std::vector<std::array<s16, 3>>{{1, 2, 3}, {-4, -5, -6}});
    REQUIRE(header.paths[1].points == std::vector<std::array<s16, 3>>{{70, 80, 90}});
}

TEST_CASE("a scene header points at its collision and lists its exits", "[scene][header]") {
    SegmentBuilder scene(hackeroot::SceneSegment);
    scene.addCommand(0x03, 0, 0x0200'0400);
    scene.addCommand(0x13, 0, 0x0200'0018);
    scene.addCommand(0x14, 0, 0);
    for (const s16 entrance : {s16{0x00DB}, s16{0x0195}, s16{0x0000}}) {
        scene.addHalf(entrance);
    }
    z64::MemoryImage image = scene.makeImage();
    image.symbols.emplace(
        "sceneExits",
        z64::ImageSymbol{.segment = hackeroot::SceneSegment, .offset = 0x18, .size = 6});

    const toki::scene::SceneHeader header = toki::scene::readSceneHeader(image);

    REQUIRE(header.diagnostics.empty());
    REQUIRE(header.collisionHeader == 0x0200'0400);
    REQUIRE(header.exits == std::vector<u16>{0x00DB, 0x0195, 0x0000});
}
