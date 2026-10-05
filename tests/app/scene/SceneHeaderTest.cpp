#include "app/scene/domain/SceneHeader.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/SceneFiles.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <vector>

using base::s16;
using base::s8;
using base::u16;
using base::u32;
using base::u8;
using base::usize;

namespace {

/// One segment's bytes written by hand, big-endian, the way the build lays out
/// a scene or room header and the data it points at.
class SegmentBuilder {
public:
    explicit SegmentBuilder(const u8 number) : number(number) {}

    [[nodiscard]] u32 getAddress() const {
        return (u32{number} << 24) | static_cast<u32>(bytes.size());
    }

    void addBytes(const std::initializer_list<u8> values) {
        bytes.insert(bytes.end(), values);
    }

    void addHalf(const s16 value) {
        bytes.push_back(static_cast<u8>(static_cast<u16>(value) >> 8));
        bytes.push_back(static_cast<u8>(value));
    }

    void addWord(const u32 value) {
        for (const int shift : {24, 16, 8, 0}) {
            bytes.push_back(static_cast<u8>(value >> shift));
        }
    }

    void addCommand(const u8 code, const u8 data1, const u32 data2) {
        addBytes({code, data1, 0, 0});
        addWord(data2);
    }

    void setWord(const usize offset, const u32 value) {
        for (usize i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<u8>(value >> (24 - (i * 8)));
        }
    }

    [[nodiscard]] z64::MemoryImage makeImage() const {
        z64::MemoryImage image;
        image.segments.push_back(z64::ImageSegment{.number = number, .bytes = bytes});
        return image;
    }

private:
    u8 number;
    std::vector<u8> bytes;
};

} // namespace

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
