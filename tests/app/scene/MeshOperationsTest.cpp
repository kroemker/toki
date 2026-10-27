#include "app/scene/domain/MeshOperations.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using base::f32;
using base::s16;
using base::s8;
using base::u32;
using base::u8;
using base::usize;
using toki::scene::MeshOperation;
using toki::scene::MeshPick;

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

u32 packTriangle(const u32 a, const u32 b, const u32 c) {
    return (a * 2 << 16) | (b * 2 << 8) | (c * 2);
}

/// A lit strip of two quads on the floor, normals up, the middle edge's
/// corners shared by both quads' faces.
toki::scene::Scene makeScene() {
    std::vector<u8> vertices;
    for (const std::array<s16, 3>& position : std::vector<std::array<s16, 3>>{
             {0, 0, 0}, {0, 0, 100}, {100, 0, 100}, {100, 0, 0}, {200, 0, 100}, {200, 0, 0}}) {
        for (const s16 axis : position) {
            addHalf(vertices, axis);
        }
        addHalf(vertices, 0);
        addHalf(vertices, position[0]);
        addHalf(vertices, position[2]);
        addWord(vertices, 0x007F'00FFU);
    }
    std::vector<u8> list;
    addWord(list, 0xD900'0000U);
    addWord(list, 0x0002'0000U);
    addWord(list, 0x0100'0000U | (6U << 12) | (6U << 1));
    addWord(list, 0);
    addWord(list, 0x0600'0000U | packTriangle(0, 1, 2));
    addWord(list, packTriangle(0, 2, 3));
    addWord(list, 0x0600'0000U | packTriangle(3, 2, 4));
    addWord(list, packTriangle(3, 4, 5));
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

/// What `operation` makes of the scene, read back once its rebuild is made.
struct Operated {
    toki::scene::Scene scene;
    toki::scene::SceneMesh mesh;
};

Operated operate(const toki::scene::Scene& scene, const MeshOperation operation,
                 const MeshPick& pick) {
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<toki::scene::OperatedRoom> operated =
        toki::scene::operateMesh(mesh, operation, std::span(&pick, 1));
    REQUIRE(operated.size() == 1);
    const std::array<toki::scene::RoomDraft, 1> drafts{operated[0].draft};
    toki::scene::SceneModel model(scene);
    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, model, mesh, drafts);
    REQUIRE(rebuild.has_value());
    REQUIRE(toki::scene::applyMeshRebuild(model, *rebuild).has_value());
    Operated made{.scene = scene};
    made.scene.images = model.layOut();
    made.scene.layers[0].rooms = toki::scene::findMovedRooms(scene, 0, made.scene.images);
    made.mesh = toki::scene::readSceneMesh(made.scene, 0, &rebuild->kept);
    return made;
}

/// The vertex of `mesh` at `position`.
usize findVertexAt(const toki::scene::RoomMesh& mesh, const glm::vec3& position) {
    for (usize vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        if (mesh.vertices[vertex].position == position) {
            return vertex;
        }
    }
    return mesh.vertices.size();
}

} // namespace

TEST_CASE("faces deleted are taken out of the list that drew them", "[scene][mesh]") {
    const Operated made = operate(makeScene(), MeshOperation::Delete,
                                  MeshPick{.room = 0, .touchedFaces = {2, 3}});

    REQUIRE(made.mesh.rooms[0].faces.size() == 2);
    REQUIRE(findVertexAt(made.mesh.rooms[0], {200.0F, 0.0F, 0.0F}) ==
            made.mesh.rooms[0].vertices.size());
}

TEST_CASE("faces flipped face the other way and their lit vertices turn with them",
          "[scene][mesh]") {
    const Operated made =
        operate(makeScene(), MeshOperation::Flip, MeshPick{.room = 0, .faces = {0, 1, 2, 3}});

    const toki::scene::RoomMesh& room = made.mesh.rooms[0];
    REQUIRE(room.faces.size() == 4);
    const toki::scene::MeshEntry& entry = room.entries[room.faces[0].corners[0]];
    REQUIRE(static_cast<s8>(entry.vtx[13]) < 0);
    const glm::vec3 first = room.vertices[room.faces[0].vertices[0]].position;
    REQUIRE(room.vertices[room.faces[0].vertices[1]].position == glm::vec3(100.0F, 0.0F, 100.0F));
    REQUIRE(first == glm::vec3(0.0F, 0.0F, 0.0F));
}

TEST_CASE("vertices merged are one vertex at their middle, and faces they flatten go",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const toki::scene::RoomMesh& room = mesh.rooms[0];
    const usize corner = findVertexAt(room, {200.0F, 0.0F, 100.0F});
    const usize other = findVertexAt(room, {200.0F, 0.0F, 0.0F});

    const Operated made = operate(scene, MeshOperation::Merge,
                                  MeshPick{.room = 0, .vertices = {corner, other}});

    REQUIRE(made.mesh.rooms[0].faces.size() == 3);
    REQUIRE(findVertexAt(made.mesh.rooms[0], {200.0F, 0.0F, 50.0F}) <
            made.mesh.rooms[0].vertices.size());
}

TEST_CASE("faces split off keep their place but no longer share vertices with the rest",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const usize before = toki::scene::readSceneMesh(scene, 0).rooms[0].vertices.size();

    const Operated made =
        operate(scene, MeshOperation::Split, MeshPick{.room = 0, .faces = {2, 3}});

    const toki::scene::RoomMesh& room = made.mesh.rooms[0];
    REQUIRE(room.faces.size() == 4);
    REQUIRE(room.vertices.size() == before + 2);
    REQUIRE(room.faces[0].vertices[2] != room.faces[2].vertices[1]);
    REQUIRE(room.vertices[room.faces[0].vertices[2]].position ==
            room.vertices[room.faces[2].vertices[1]].position);
}
