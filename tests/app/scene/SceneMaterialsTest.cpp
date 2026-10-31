#include "app/scene/domain/SceneMaterials.h"

#include "app/scene/domain/ComponentSummary.h"
#include "app/scene/domain/GeometryChanges.h"
#include "app/scene/domain/MaterialBindings.h"
#include "app/scene/domain/MaterialEdits.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/MeshShapes.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using base::s16;
using base::u32;
using base::u8;
using base::usize;
using toki::scene::SceneMaterial;

namespace {

z64::ImageObject makeObject(std::string name, std::string type, std::vector<u8> bytes,
                            const u8 segment = 3, std::vector<z64::Relocation> relocations = {}) {
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

std::vector<u8> makeVertices(const s16 x) {
    const auto far = static_cast<s16>(x + 100);
    std::vector<u8> vertices;
    for (const std::array<s16, 3>& position :
         std::vector<std::array<s16, 3>>{{x, 0, 0}, {x, 0, 100}, {far, 0, 100}, {far, 0, 0}}) {
        for (const s16 axis : position) {
            addHalf(vertices, axis);
        }
        addHalf(vertices, 0);
        addHalf(vertices, 0);
        addHalf(vertices, 0);
        addWord(vertices, 0xFFFF'FFFFU);
    }
    return vertices;
}

std::vector<u8> makeMaterialList(const u32 combine) {
    std::vector<u8> list;
    addWord(list, 0xFC00'0000U | combine);
    addWord(list, 0);
    addWord(list, 0xDF00'0000U);
    addWord(list, 0);
    return list;
}

/// Two quads of a room, each drawn under a material list of its own, the
/// names of its symbols starting `prefix`.
std::shared_ptr<const z64::MemoryImage> makeRoom(const std::string& prefix, const s16 x) {
    const std::string first = std::format("mat_{}_dl_stone_layerOpaque", prefix);
    const std::string second = std::format("mat_{}_dl_grass_layerOpaque", prefix);
    std::vector<u8> list;
    std::vector<z64::Relocation> relocations;
    for (const auto& [material, vertices] :
         std::array{std::pair{first, prefix + "_vtx_0"}, std::pair{second, prefix + "_vtx_1"}}) {
        addWord(list, 0xDE00'0000U);
        relocations.push_back({.offset = list.size(), .symbol = material});
        addWord(list, 0);
        addWord(list, 0x0100'0000U | (4U << 12) | (4U << 1));
        relocations.push_back({.offset = list.size(), .symbol = vertices});
        addWord(list, 0);
        addWord(list, 0x0600'0000U | (2U << 8) | 4U);
        addWord(list, (4U << 8) | 6U);
    }
    addWord(list, 0xDF00'0000U);
    addWord(list, 0);
    return std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(first, "Gfx", makeMaterialList(1)),
         makeObject(second, "Gfx", makeMaterialList(2)),
         makeObject(prefix + "_vtx_0", "Vtx", makeVertices(x)),
         makeObject(prefix + "_vtx_1", "Vtx", makeVertices(static_cast<s16>(x + 200))),
         makeObject(prefix + "_dl", "Gfx", std::move(list), 3, std::move(relocations))},
        {}));
}

/// A scene of `rooms` rooms as `makeRoom` makes them, side by side.
toki::scene::Scene makeScene(const usize rooms = 1) {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    toki::scene::SceneLayer layer;
    for (usize room = 0; room < rooms; ++room) {
        const std::string prefix = room == 0 ? "room" : std::format("room{}", room);
        const auto image = makeRoom(prefix, static_cast<s16>(room * 1000));
        scene.images.push_back(image);
        toki::scene::RoomHeader header;
        header.displayLists.push_back({.opaque = *image->findAddress(prefix + "_dl")});
        layer.rooms.push_back(header);
    }
    scene.layers.push_back(std::move(layer));
    return scene;
}

/// What `operated` makes of `scene`, read back once its rebuild is made.
toki::scene::SceneMesh makeRebuilt(const toki::scene::Scene& scene,
                                   const toki::scene::SceneMesh& mesh,
                                   const std::vector<toki::scene::OperatedRoom>& operated);

const SceneMaterial& findMaterial(const std::vector<SceneMaterial>& materials,
                                  const std::string_view name) {
    const auto found = std::ranges::find(materials, name, &SceneMaterial::name);
    REQUIRE(found != materials.end());
    return *found;
}

toki::scene::Scene makeLaidOut(const toki::scene::Scene& scene,
                               const toki::scene::SceneModel& model) {
    toki::scene::Scene made = scene;
    made.images = model.layOut();
    made.layers[0].rooms = toki::scene::findMovedRooms(scene, 0, made.images);
    return made;
}

toki::scene::SceneMesh makeRebuilt(const toki::scene::Scene& scene,
                                   const toki::scene::SceneMesh& mesh,
                                   const std::vector<toki::scene::OperatedRoom>& operated) {
    std::vector<toki::scene::RoomDraft> drafts;
    for (const toki::scene::OperatedRoom& room : operated) {
        drafts.push_back(room.draft);
    }
    toki::scene::SceneModel model(scene);
    const auto rebuild = toki::scene::planMeshRebuild(scene, model, mesh, drafts);
    REQUIRE(rebuild.has_value());
    REQUIRE(toki::scene::applyMeshRebuild(model, *rebuild).has_value());
    return toki::scene::readSceneMesh(makeLaidOut(scene, model), 0, &rebuild->kept);
}

} // namespace

TEST_CASE("the names fast64 gives materials are read out of and written into their lists",
          "[scene][mesh]") {
    REQUIRE(toki::scene::findMaterialName("mat_spot_room_0_dl_Wall_Tex_layerOpaque") == "Wall_Tex");
    REQUIRE(toki::scene::renameMaterialList("mat_room_dl_stone_layerOpaque", "brick") ==
            "mat_room_dl_brick_layerOpaque");
    REQUIRE(toki::scene::makeMaterialListName("room", "sand", true) ==
            "mat_room_dl_sand_layerTransparent");
}

TEST_CASE("faces are listed under the materials their lists set", "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, mesh);

    REQUIRE(materials.size() == 2);
    REQUIRE(findMaterial(materials, "stone").countFaces() == 2);
    REQUIRE(findMaterial(materials, "grass").countFaces() == 2);
    REQUIRE(findMaterial(materials, "grass").isListed());
    REQUIRE(toki::scene::findFaceMaterial(materials, mesh.rooms[0].faces[0]).has_value());
}

TEST_CASE("faces assigned a material are drawn in its run", "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, mesh);
    const toki::scene::MeshPick pick{.room = 0, .faces = {0}};

    const auto operated =
        toki::scene::assignMaterial(mesh, std::span(&pick, 1), findMaterial(materials, "grass"));
    REQUIRE(operated.has_value());
    toki::scene::SceneModel model(scene);
    const std::array<toki::scene::RoomDraft, 1> drafts{(*operated)[0].draft};
    const auto rebuild = toki::scene::planMeshRebuild(scene, model, mesh, drafts);
    REQUIRE(rebuild.has_value());
    REQUIRE(toki::scene::applyMeshRebuild(model, *rebuild).has_value());

    const toki::scene::Scene made = makeLaidOut(scene, model);
    const toki::scene::SceneMesh read = toki::scene::readSceneMesh(made, 0, &rebuild->kept);
    const std::vector<SceneMaterial> after = toki::scene::listSceneMaterials(made, read);
    REQUIRE(findMaterial(after, "stone").countFaces() == 1);
    REQUIRE(findMaterial(after, "grass").countFaces() == 3);
}

TEST_CASE("a material renamed renames its list and what points at it, and one duplicated is "
          "listed undrawn",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, mesh);
    toki::scene::SceneModel model(scene);

    model.setMaterialBindings(toki::scene::MaterialBindings{
        .materials = {{"grass", toki::scene::MaterialBinding{.isSolid = true}}}});
    REQUIRE(toki::scene::renameMaterial(model, findMaterial(materials, "grass"), "moss"));
    CHECK(model.getMaterialBindings().materials.contains("moss"));
    CHECK_FALSE(model.getMaterialBindings().materials.contains("grass"));
    REQUIRE(toki::scene::duplicateMaterial(model, findMaterial(materials, "stone"), "brick"));
    REQUIRE_FALSE(toki::scene::renameMaterial(model, findMaterial(materials, "stone"), "a b"));

    const toki::scene::Scene made = makeLaidOut(scene, model);
    const std::vector<SceneMaterial> after =
        toki::scene::listSceneMaterials(made, toki::scene::readSceneMesh(made, 0));
    REQUIRE(findMaterial(after, "moss").countFaces() == 2);
    REQUIRE(findMaterial(after, "brick").countFaces() == 0);
    REQUIRE(std::ranges::find(after, "grass", &SceneMaterial::name) == after.end());
}

TEST_CASE("a box drawn like a face of the draft it is added to is drawn as that face was",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(mesh.rooms[0]);
    draft.faces.shrink_to_fit();
    const toki::scene::DraftFace like = draft.faces[2];
    const std::vector<usize> added = toki::scene::addMeshShape(
        draft, toki::scene::MeshShape::Box, {50.0F, 0.0F, 50.0F}, 100.0F, draft.faces[2]);
    REQUIRE(added.size() == 12);
    for (const usize face : added) {
        REQUIRE(draft.faces[face].draw == like.draw);
        REQUIRE(draft.faces[face].material == like.material);
    }
}

TEST_CASE("a box added to a room is drawn in the run of the face it is drawn like, closed and "
          "faced outward",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    toki::scene::OperatedRoom operated{.draft = toki::scene::makeRoomDraft(mesh.rooms[0])};
    const toki::scene::DraftFace like = operated.draft.faces[2];
    operated.faces = toki::scene::addMeshShape(operated.draft, toki::scene::MeshShape::Box,
                                               {50.0F, 0.0F, 50.0F}, 100.0F, like);
    REQUIRE(operated.faces.size() == 12);

    const toki::scene::SceneMesh read = makeRebuilt(scene, mesh, {operated});
    const toki::scene::RoomMesh& room = read.rooms[0];
    REQUIRE(room.faces.size() == 16);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, read);
    REQUIRE(findMaterial(materials, "grass").countFaces() == 14);
    for (const toki::scene::MeshFace& face : room.faces) {
        const glm::vec3 first = room.vertices[face.vertices[0]].position;
        const glm::vec3 facing = glm::cross(room.vertices[face.vertices[1]].position - first,
                                            room.vertices[face.vertices[2]].position - first);
        const glm::vec3 middle = (first + room.vertices[face.vertices[1]].position +
                                  room.vertices[face.vertices[2]].position) /
                                 3.0F;
        if (middle.x > 0.0F && middle.x < 100.0F && middle.y > 0.0F) {
            REQUIRE(glm::dot(facing, middle - glm::vec3(50.0F, 50.0F, 50.0F)) > 0.0F);
        }
    }
}

TEST_CASE("faces moved to another room are drawn there in the run of their material",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene(2);
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, mesh);
    REQUIRE(findMaterial(materials, "stone").rooms.size() == 2);
    const toki::scene::MeshPick pick{.room = 0, .faces = {0, 1}};

    const auto operated = toki::scene::moveFacesToRoom(mesh, std::span(&pick, 1), 1, materials);
    REQUIRE(operated.has_value());
    REQUIRE(operated->size() == 2);

    const toki::scene::SceneMesh read = makeRebuilt(scene, mesh, *operated);
    REQUIRE(read.rooms[0].faces.size() == 2);
    REQUIRE(read.rooms[1].faces.size() == 6);
    const std::vector<SceneMaterial> after = toki::scene::listSceneMaterials(scene, read);
    const SceneMaterial& stone = findMaterial(after, "stone");
    REQUIRE(stone.findRoom(1)->faces == 4);
    REQUIRE(stone.findRoom(0) == nullptr);
}

TEST_CASE("a save lists the lists a topology edit rebuilt, and none it left as read",
          "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene(2);
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    toki::scene::OperatedRoom operated{.draft = toki::scene::makeRoomDraft(mesh.rooms[0])};
    toki::scene::deleteFaces(operated.draft, std::vector<usize>{0});
    const std::array<toki::scene::RoomDraft, 1> drafts{operated.draft};
    toki::scene::SceneModel model(scene);
    const auto rebuild = toki::scene::planMeshRebuild(scene, model, mesh, drafts);
    REQUIRE(rebuild.has_value());
    REQUIRE(toki::scene::applyMeshRebuild(model, *rebuild).has_value());

    const toki::scene::GeometryChanges listed = toki::scene::listGeometryChanges(scene, model);
    const auto list =
        std::ranges::find(listed.changes, "room_dl", &toki::scene::GeometryChange::symbol);
    REQUIRE(list != listed.changes.end());
    REQUIRE(list->kind == toki::scene::GeometryChangeKind::Rebuilt);
    REQUIRE(std::ranges::find(listed.changes, "room1_dl", &toki::scene::GeometryChange::symbol) ==
            listed.changes.end());
    REQUIRE(listed.unchanged > 0);
}

TEST_CASE("picked vertices are summed up where they agree, and coloured as one", "[scene][mesh]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
    const std::vector<SceneMaterial> materials = toki::scene::listSceneMaterials(scene, mesh);
    const std::vector<toki::scene::ComponentRef> vertices{{.piece = 0, .index = 0},
                                                          {.piece = 0, .index = 1}};
    const std::vector<toki::scene::ComponentRef> faces{{.piece = 0, .index = 0},
                                                       {.piece = 0, .index = 2}};

    const toki::scene::ComponentSummary summary =
        toki::scene::summarizeComponents(mesh, materials, vertices, faces);
    REQUIRE(summary.color == std::array<u8, 3>{0xFF, 0xFF, 0xFF});
    REQUIRE(summary.alpha == u8{0xFF});
    REQUIRE_FALSE(summary.material.has_value());
    REQUIRE(summary.isTranslucent == false);

    const std::vector<toki::scene::SymbolWrite> writes =
        toki::scene::makeColorWrites(mesh, vertices, std::array<u8, 3>{1, 2, 3}, std::nullopt);
    REQUIRE(writes.size() == 2);
    REQUIRE(writes[0].bytes == std::vector<u8>{1, 2, 3});
}
