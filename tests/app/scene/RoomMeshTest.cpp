#include "app/scene/domain/RoomMesh.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using base::s16;
using base::u32;
using base::u8;
using base::usize;

namespace {

z64::ImageObject makeObject(std::string name, std::string type, std::vector<u8> bytes,
                            const u8 segment, std::vector<z64::Relocation> relocations = {}) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = std::move(type),
        .isArray = true,
        .alignment = 8,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

void addHalf(std::vector<u8>& bytes, const s16 value) {
    bytes.push_back(static_cast<u8>(static_cast<u32>(value) >> 8));
    bytes.push_back(static_cast<u8>(value));
}

void addWord(std::vector<u8>& bytes, const u32 value) {
    for (const u32 shift : {24U, 16U, 8U, 0U}) {
        bytes.push_back(static_cast<u8>(value >> shift));
    }
}

std::vector<u8> makeVertices(const std::initializer_list<std::array<s16, 3>> positions) {
    std::vector<u8> bytes;
    s16 texture = 0;
    for (const std::array<s16, 3>& position : positions) {
        for (const s16 axis : position) {
            addHalf(bytes, axis);
        }
        addHalf(bytes, 0);
        addHalf(bytes, texture);
        addHalf(bytes, texture);
        addWord(bytes, 0xFFFF'FFFF);
        texture += 32;
    }
    return bytes;
}

u32 packTriangle(const u32 a, const u32 b, const u32 c) {
    return (a * 2 << 16) | (b * 2 << 8) | (c * 2);
}

/// One room: a quad, a second quad meeting it along an edge whose two corners
/// it loads again with other texture coordinates, and a triangle that only
/// touches the first quad's corner.
toki::scene::Scene makeScene() {
    std::vector<u8> list;
    addWord(list, 0x0100'0000U | (11U << 12) | (11U << 1));
    addWord(list, 0);
    addWord(list, 0x0600'0000U | packTriangle(0, 1, 2));
    addWord(list, packTriangle(0, 2, 3));
    addWord(list, 0x0600'0000U | packTriangle(4, 6, 7));
    addWord(list, packTriangle(4, 7, 5));
    addWord(list, 0x0500'0000U | packTriangle(8, 9, 10));
    addWord(list, 0);
    addWord(list, 0xDF00'0000U);
    addWord(list, 0);

    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    const auto room = std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject("room_vtx", "Vtx",
                    makeVertices({{0, 0, 0},
                                  {100, 0, 0},
                                  {100, 0, 100},
                                  {0, 0, 100},
                                  {100, 0, 0},
                                  {100, 0, 100},
                                  {200, 0, 0},
                                  {200, 0, 100},
                                  {0, 0, 0},
                                  {-100, 0, 0},
                                  {0, 0, -100}}),
                    3),
         makeObject("room_dl", "Gfx", std::move(list), 3, {{.offset = 4, .symbol = "room_vtx"}})},
        {}));
    scene.images.push_back(room);
    toki::scene::RoomHeader header;
    header.displayLists.push_back({.opaque = *room->findAddress("room_dl")});
    scene.layers.push_back(toki::scene::SceneLayer{.rooms = {header}});
    return scene;
}

/// The vertex that holds the entry loaded from `room_vtx[index]`.
usize findVertex(const toki::scene::RoomMesh& mesh, const usize index) {
    for (usize vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        for (const usize entry : mesh.vertices[vertex].entries) {
            if (mesh.entries[entry].key.index == index) {
                return vertex;
            }
        }
    }
    return mesh.vertices.size();
}

} // namespace

TEST_CASE("a room's mesh is read from its display lists, seams joined and touching pieces not",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();

    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);

    REQUIRE(mesh.diagnostics.empty());
    REQUIRE(mesh.rooms.size() == 1);
    const toki::scene::RoomMesh& room = mesh.rooms[0];
    REQUIRE(room.entries.size() == 11);
    REQUIRE(room.faces.size() == 5);
    REQUIRE(room.vertices.size() == 9);
    REQUIRE(room.edges.size() == 12);
    REQUIRE(room.entries[7].key == toki::scene::MeshEntryKey{.symbol = "room_vtx", .index = 5});
    REQUIRE(room.entries[7].stored == std::array<s16, 3>{100, 0, 100});
    REQUIRE(findVertex(room, 1) == findVertex(room, 4));
    REQUIRE(findVertex(room, 2) == findVertex(room, 5));
    REQUIRE(findVertex(room, 0) != findVertex(room, 8));
    REQUIRE(room.vertices[findVertex(room, 9)].position == glm::vec3(-100.0F, 0.0F, 0.0F));
    REQUIRE(toki::scene::findFaceCenter(room, room.faces[4]) ==
            glm::vec3(-100.0F / 3.0F, 0.0F, -100.0F / 3.0F));
}

TEST_CASE("a mesh read again keeps the vertices it was given, whatever stands where",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneMesh kept = toki::scene::readSceneMesh(scene, 0);
    toki::scene::RoomMesh& room = kept.rooms[0];
    toki::scene::MeshVertex& seam = room.vertices[findVertex(room, 1)];
    seam.entries = {1};
    room.vertices.push_back(toki::scene::MeshVertex{.entries = {4}});
    REQUIRE(room.entries[4].key.index == 4);

    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(scene, 0, &kept);

    REQUIRE(read.rooms[0].vertices.size() == 10);
    REQUIRE(findVertex(read.rooms[0], 1) != findVertex(read.rooms[0], 4));
    REQUIRE(findVertex(read.rooms[0], 2) == findVertex(read.rooms[0], 5));
}
