#include "app/scene/domain/MeshEdits.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Types.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using base::s16;
using base::s8;
using base::u16;
using base::u32;
using base::u8;
using base::usize;
using toki::scene::MeshVertexMove;

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

/// A lit quad on the floor of room 0, its two triangles loading the corners
/// of their shared edge twice, and the normals pointing down.
toki::scene::Scene makeScene() {
    std::vector<u8> vertices;
    for (const std::array<s16, 3>& position : std::initializer_list<std::array<s16, 3>>{
             {0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, 0, 0}, {100, 0, 100}, {0, 0, 100}}) {
        for (const s16 axis : position) {
            addHalf(vertices, axis);
        }
        addHalf(vertices, 0);
        addWord(vertices, 0);
        addWord(vertices, 0x0081'00FFU);
    }
    std::vector<u8> list;
    addWord(list, 0xD900'0000U);
    addWord(list, 0x0002'0000U);
    addWord(list, 0x0100'0000U | (6U << 12) | (6U << 1));
    addWord(list, 0);
    addWord(list, 0x0600'0000U | (0U << 16) | (4U << 8) | 2U);
    addWord(list, (6U << 16) | (10U << 8) | 8U);
    addWord(list, 0xDF00'0000U);
    addWord(list, 0);

    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    const auto room = std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject("room_vtx", "Vtx", std::move(vertices), 3),
         makeObject("room_dl", "Gfx", std::move(list), 3, {{.offset = 12, .symbol = "room_vtx"}})},
        {}));
    scene.images.push_back(room);
    toki::scene::RoomHeader header;
    header.displayLists.push_back({.opaque = *room->findAddress("room_dl")});
    scene.layers.push_back(toki::scene::SceneLayer{.rooms = {header}});
    return scene;
}

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

std::array<s8, 3> readNormal(const z64::MemoryImage& image, const usize index) {
    const std::span<const u8> bytes = *image.findBytes(*image.findAddress("room_vtx"), 6 * 16);
    return {static_cast<s8>(bytes[(index * 16) + 12]), static_cast<s8>(bytes[(index * 16) + 13]),
            static_cast<s8>(bytes[(index * 16) + 14])};
}

} // namespace

TEST_CASE("a vertex moved writes every Vtx it stands for, and the normals of its faces",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    REQUIRE(mesh.diagnostics.empty());
    const toki::scene::RoomMesh& room = mesh.rooms[0];
    REQUIRE(room.vertices.size() == 4);
    const usize corner = findVertex(room, 2);
    REQUIRE(corner == findVertex(room, 4));
    const std::array<MeshVertexMove, 1> moves{
        {{.room = 0, .vertex = corner, .position = {100.0F, 50.0F, 100.0F}}}};

    const std::vector<toki::scene::SymbolWrite> writes =
        toki::scene::makeMeshMoveWrites(scene, mesh, moves);
    toki::scene::Scene written = scene;
    written.images = toki::scene::makeWrittenImages(scene, writes);
    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(written, 0, &mesh);

    const toki::scene::RoomMesh& moved = read.rooms[0];
    REQUIRE(moved.vertices[findVertex(moved, 2)].position == glm::vec3(100.0F, 50.0F, 100.0F));
    REQUIRE(findVertex(moved, 2) == findVertex(moved, 4));
    REQUIRE(scene.images[2]->findBytes(*scene.images[2]->findAddress("room_vtx"), 16).has_value());
    const std::array<s8, 3> before = readNormal(*scene.images[2], 0);
    const std::array<s8, 3> after = readNormal(*written.images[2], 0);
    REQUIRE(before == std::array<s8, 3>{0, -127, 0});
    REQUIRE(after != before);
    REQUIRE(after[1] > 0);
    REQUIRE(toki::scene::makeMovedMesh(mesh, moves).rooms[0].vertices[corner].position ==
            glm::vec3(100.0F, 50.0F, 100.0F));
}

TEST_CASE("symbol writes change a scene model's symbols, and not past their end", "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneModel model(scene);
    const std::array<toki::scene::SymbolWrite, 1> writes{
        {{.symbol = "room_vtx", .offset = 2, .bytes = {0x12, 0x34}}}};

    REQUIRE(toki::scene::applySymbolWrites(model, writes).has_value());
    REQUIRE(n64::readU16(model.findSymbol("room_vtx")->bytes, 2) == 0x1234);

    const std::array<toki::scene::SymbolWrite, 1> past{
        {{.symbol = "room_vtx", .offset = 95, .bytes = {1, 2}}}};
    REQUIRE_FALSE(toki::scene::applySymbolWrites(model, past).has_value());
}
