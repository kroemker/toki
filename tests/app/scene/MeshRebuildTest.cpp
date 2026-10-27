#include "app/scene/domain/MeshRebuild.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

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

void addVertex(std::vector<u8>& bytes, const std::array<s16, 3>& position) {
    for (const s16 axis : position) {
        addHalf(bytes, axis);
    }
    addHalf(bytes, 0);
    addHalf(bytes, position[0]);
    addHalf(bytes, position[2]);
    addWord(bytes, 0xFFFF'FFFF);
}

u32 packTriangle(const u32 a, const u32 b, const u32 c) {
    return (a * 2 << 16) | (b * 2 << 8) | (c * 2);
}

void addLoad(std::vector<u8>& list, const u32 count, const u32 first = 0) {
    addWord(list, 0x0100'0000U | (count << 12) | ((first + count) << 1));
    addWord(list, first * 16);
}

/// A room drawing `list` and then `other`, each loading from its own array.
struct RoomParts {
    std::vector<u8> vertices;
    std::vector<u8> list;
    std::vector<z64::Relocation> relocations;
    std::vector<u8> otherVertices;
    std::vector<u8> other;
};

toki::scene::Scene makeScene(RoomParts parts) {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    std::vector<z64::ImageObject> objects{
        makeObject("room_vtx", "Vtx", std::move(parts.vertices), 3),
        makeObject("room_dl", "Gfx", std::move(parts.list), 3, std::move(parts.relocations))};
    const bool hasOther = !parts.other.empty();
    if (hasOther) {
        objects.push_back(makeObject("other_vtx", "Vtx", std::move(parts.otherVertices), 3));
        objects.push_back(makeObject("other_dl", "Gfx", std::move(parts.other), 3,
                                     {{.offset = 4, .symbol = "other_vtx"}}));
    }
    const auto room = std::make_shared<const z64::MemoryImage>(z64::layOutImage(objects, {}));
    scene.images.push_back(room);
    toki::scene::RoomHeader header;
    header.displayLists.push_back({.opaque = *room->findAddress("room_dl")});
    if (hasOther) {
        header.displayLists.push_back({.opaque = *room->findAddress("other_dl")});
    }
    scene.layers.push_back(toki::scene::SceneLayer{.rooms = {header}});
    return scene;
}

/// A quad and a triangle in one run, after a state command, and a lone
/// triangle drawn by another list.
toki::scene::Scene makeQuadScene() {
    RoomParts parts;
    for (const std::array<s16, 3>& position : std::vector<std::array<s16, 3>>{
             {0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, 0, 100}, {200, 0, 0}}) {
        addVertex(parts.vertices, position);
    }
    addWord(parts.list, 0xE700'0000U);
    addWord(parts.list, 0);
    addLoad(parts.list, 5);
    addWord(parts.list, 0x0600'0000U | packTriangle(0, 1, 2));
    addWord(parts.list, packTriangle(0, 2, 3));
    addWord(parts.list, 0x0500'0000U | packTriangle(1, 4, 2));
    addWord(parts.list, 0);
    addWord(parts.list, 0xDF00'0000U);
    addWord(parts.list, 0);
    parts.relocations = {{.offset = 12, .symbol = "room_vtx"}};
    for (const std::array<s16, 3>& position :
         std::vector<std::array<s16, 3>>{{0, 50, 0}, {10, 50, 0}, {0, 50, 10}}) {
        addVertex(parts.otherVertices, position);
    }
    addLoad(parts.other, 3);
    addWord(parts.other, 0x0500'0000U | packTriangle(0, 1, 2));
    addWord(parts.other, 0);
    addWord(parts.other, 0xDF00'0000U);
    addWord(parts.other, 0);
    return makeScene(std::move(parts));
}

std::expected<toki::scene::Scene, base::Diagnostic>
makeWritten(const toki::scene::Scene& scene, const toki::scene::MeshRebuild& rebuild) {
    toki::scene::SceneModel model(scene);
    if (std::expected<void, base::Diagnostic> applied =
            toki::scene::applyMeshRebuild(model, rebuild);
        !applied) {
        return std::unexpected(applied.error());
    }
    toki::scene::Scene written = scene;
    written.images = model.layOut();
    written.layers[0].rooms = toki::scene::findMovedRooms(scene, 0, written.images);
    return written;
}

} // namespace

TEST_CASE("a draft that changes nothing rebuilds nothing", "[scene][mesh]") {
    const toki::scene::Scene scene = makeQuadScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    REQUIRE(mesh.rooms[0].faces.size() == 4);
    const std::array<toki::scene::RoomDraft, 1> drafts{toki::scene::makeRoomDraft(mesh.rooms[0])};

    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, toki::scene::SceneModel(scene), mesh, drafts);

    REQUIRE(rebuild.has_value());
    REQUIRE(rebuild->isEmpty());
}

TEST_CASE("a face taken out rebuilds its run alone, and lists it is not in keep their bytes",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeQuadScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const toki::scene::SceneModel model(scene);
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(mesh.rooms[0]);
    draft.faces.erase(draft.faces.begin() + 2);
    const std::array<toki::scene::RoomDraft, 1> drafts{draft};

    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, model, mesh, drafts);

    REQUIRE(rebuild.has_value());
    REQUIRE(rebuild->lists == std::vector<std::string>{"room_dl"});
    REQUIRE(rebuild->added.empty());
    REQUIRE(rebuild->removed.empty());
    const std::expected<toki::scene::Scene, base::Diagnostic> written =
        makeWritten(scene, *rebuild);
    REQUIRE(written.has_value());
    toki::scene::SceneModel writtenModel(*written);
    REQUIRE(writtenModel.findSymbol("other_dl")->bytes == model.findSymbol("other_dl")->bytes);
    const z64::SymbolData& list = *writtenModel.findSymbol("room_dl");
    REQUIRE(n64::readU32(list.bytes, 0) == 0xE700'0000U);
    REQUIRE(list.bytes.size() == model.findSymbol("room_dl")->bytes.size() - 8);
    REQUIRE(writtenModel.findSymbol("room_vtx")->bytes.size() == 4 * 16);

    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(*written, 0, &rebuild->kept);
    REQUIRE(read.diagnostics.empty());
    REQUIRE(read.rooms[0].faces.size() == 3);
    REQUIRE(read.rooms[0].faces[0].vertices[0] == read.rooms[0].faces[1].vertices[0]);
}

TEST_CASE("a run of more vertices than the buffer holds is loaded 32 at a time", "[scene][mesh]") {
    RoomParts parts;
    for (s16 i = 0; i < 4; ++i) {
        addVertex(parts.vertices, {static_cast<s16>(i * 100), 0, 0});
    }
    addLoad(parts.list, 3);
    addWord(parts.list, 0x0500'0000U | packTriangle(0, 1, 2));
    addWord(parts.list, 0);
    addWord(parts.list, 0xDF00'0000U);
    addWord(parts.list, 0);
    parts.relocations = {{.offset = 4, .symbol = "room_vtx"}};
    const toki::scene::Scene scene = makeScene(std::move(parts));
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(mesh.rooms[0]);
    const toki::scene::DraftFace pattern = draft.faces[0];
    for (s16 i = 1; i < 20; ++i) {
        toki::scene::DraftFace face = pattern;
        face.original.reset();
        for (usize corner = 0; corner < 3; ++corner) {
            const glm::vec3 position(static_cast<f32>(corner) * 10.0F, static_cast<f32>(i) * 10.0F,
                                     0.0F);
            face.corners[corner].vertex = toki::scene::addDraftVertex(draft, position);
        }
        draft.faces.push_back(face);
    }
    const std::array<toki::scene::RoomDraft, 1> drafts{draft};

    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, toki::scene::SceneModel(scene), mesh, drafts);

    REQUIRE(rebuild.has_value());
    const std::expected<toki::scene::Scene, base::Diagnostic> written =
        makeWritten(scene, *rebuild);
    REQUIRE(written.has_value());
    const toki::scene::SceneModel writtenModel(*written);
    const z64::SymbolData& list = *writtenModel.findSymbol("room_dl");
    usize loads = 0;
    for (usize command = 0; command < list.bytes.size() / 8; ++command) {
        if (list.bytes[command * 8] == 0x01) {
            ++loads;
            REQUIRE(((n64::readU32(list.bytes, command * 8) >> 12) & 0xFFU) <= 32);
        }
    }
    REQUIRE(loads == 2);
    REQUIRE(writtenModel.findSymbol("room_vtx")->bytes.size() == 60 * 16);
    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(*written, 0, &rebuild->kept);
    REQUIRE(read.rooms[0].faces.size() == 20);
    REQUIRE(read.rooms[0].vertices.size() == 60);
}

TEST_CASE("a run that draws what an earlier one loaded is rebuilt with it", "[scene][mesh]") {
    RoomParts parts;
    for (const std::array<s16, 3>& position : std::vector<std::array<s16, 3>>{
             {0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, 0, 100}}) {
        addVertex(parts.vertices, position);
    }
    addLoad(parts.list, 4);
    addWord(parts.list, 0x0500'0000U | packTriangle(0, 1, 2));
    addWord(parts.list, 0);
    addWord(parts.list, 0xFA00'0000U);
    addWord(parts.list, 0xFFFF'FFFFU);
    addWord(parts.list, 0x0500'0000U | packTriangle(0, 2, 3));
    addWord(parts.list, 0);
    addWord(parts.list, 0xDF00'0000U);
    addWord(parts.list, 0);
    parts.relocations = {{.offset = 4, .symbol = "room_vtx"}};
    const toki::scene::Scene scene = makeScene(std::move(parts));
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    REQUIRE(mesh.rooms[0].faces.size() == 2);
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(mesh.rooms[0]);
    draft.faces.erase(draft.faces.begin());
    const std::array<toki::scene::RoomDraft, 1> drafts{draft};

    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, toki::scene::SceneModel(scene), mesh, drafts);

    REQUIRE(rebuild.has_value());
    const std::expected<toki::scene::Scene, base::Diagnostic> written =
        makeWritten(scene, *rebuild);
    REQUIRE(written.has_value());
    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(*written, 0, &rebuild->kept);
    REQUIRE(read.rooms[0].faces.size() == 1);
    REQUIRE(read.rooms[0].vertices[read.rooms[0].faces[0].vertices[2]].position ==
            glm::vec3(0.0F, 0.0F, 100.0F));
    const toki::scene::SceneModel writtenModel(*written);
    REQUIRE(writtenModel.findSymbol("room_dl")->bytes[0] == 0xFA);
}

TEST_CASE("what points past a rebuilt run is moved with the commands after it", "[scene][mesh]") {
    RoomParts parts;
    for (const std::array<s16, 3>& position :
         std::vector<std::array<s16, 3>>{{0, 0, 0}, {100, 0, 0}, {100, 0, 100}}) {
        addVertex(parts.vertices, position);
    }
    addLoad(parts.list, 3);
    addWord(parts.list, 0x0500'0000U | packTriangle(0, 1, 2));
    addWord(parts.list, 0);
    addWord(parts.list, 0x0500'0000U | packTriangle(0, 2, 1));
    addWord(parts.list, 0);
    addWord(parts.list, 0xDF00'0000U);
    addWord(parts.list, 0);
    parts.relocations = {{.offset = 4, .symbol = "room_vtx"}};
    addWord(parts.other, 0xDE01'0000U);
    addWord(parts.other, 24);
    addWord(parts.other, 0xDF00'0000U);
    addWord(parts.other, 0);
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    const auto room = std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject("room_vtx", "Vtx", std::move(parts.vertices), 3),
         makeObject("room_dl", "Gfx", std::move(parts.list), 3, std::move(parts.relocations)),
         makeObject("tail_dl", "Gfx", std::move(parts.other), 3, {{.offset = 4, .symbol = "room_dl"}})},
        {}));
    scene.images.push_back(room);
    toki::scene::RoomHeader header;
    header.displayLists.push_back({.opaque = *room->findAddress("room_dl")});
    scene.layers.push_back(toki::scene::SceneLayer{.rooms = {header}});
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(mesh.rooms[0]);
    draft.faces.pop_back();
    const std::array<toki::scene::RoomDraft, 1> drafts{draft};

    const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
        toki::scene::planMeshRebuild(scene, toki::scene::SceneModel(scene), mesh, drafts);

    REQUIRE(rebuild.has_value());
    const std::expected<toki::scene::Scene, base::Diagnostic> written =
        makeWritten(scene, *rebuild);
    REQUIRE(written.has_value());
    const toki::scene::SceneModel writtenModel(*written);
    REQUIRE(n64::readU32(writtenModel.findSymbol("tail_dl")->bytes, 4) == 16);
}
