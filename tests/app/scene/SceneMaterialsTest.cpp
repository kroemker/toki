#include "app/scene/domain/SceneMaterials.h"

#include "app/scene/domain/MaterialEdits.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
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

constexpr std::string_view FirstList = "mat_room_dl_stone_layerOpaque";
constexpr std::string_view SecondList = "mat_room_dl_grass_layerOpaque";

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

/// Two quads of a room, each drawn under a material list of its own.
toki::scene::Scene makeScene() {
    std::vector<u8> list;
    std::vector<z64::Relocation> relocations;
    for (const auto& [material, vertices] :
         std::array{std::pair{FirstList, "room_vtx_0"}, std::pair{SecondList, "room_vtx_1"}}) {
        addWord(list, 0xDE00'0000U);
        relocations.push_back({.offset = list.size(), .symbol = std::string(material)});
        addWord(list, 0);
        addWord(list, 0x0100'0000U | (4U << 12) | (4U << 1));
        relocations.push_back({.offset = list.size(), .symbol = vertices});
        addWord(list, 0);
        addWord(list, 0x0600'0000U | (2U << 8) | 4U);
        addWord(list, (4U << 8) | 6U);
    }
    addWord(list, 0xDF00'0000U);
    addWord(list, 0);

    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("code", "u8", std::vector<u8>(8, 0), 0)}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject("scene", "u8", std::vector<u8>(8, 0), 2)}, {})));
    const auto room = std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(std::string(FirstList), "Gfx", makeMaterialList(1)),
                          makeObject(std::string(SecondList), "Gfx", makeMaterialList(2)),
                          makeObject("room_vtx_0", "Vtx", makeVertices(0)),
                          makeObject("room_vtx_1", "Vtx", makeVertices(200)),
                          makeObject("room_dl", "Gfx", std::move(list), 3, std::move(relocations))},
                         {}));
    scene.images.push_back(room);
    toki::scene::RoomHeader header;
    header.displayLists.push_back({.opaque = *room->findAddress("room_dl")});
    scene.layers.push_back(toki::scene::SceneLayer{.rooms = {header}});
    return scene;
}

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

    REQUIRE(toki::scene::renameMaterial(model, findMaterial(materials, "grass"), "moss"));
    REQUIRE(toki::scene::duplicateMaterial(model, findMaterial(materials, "stone"), "brick"));
    REQUIRE_FALSE(toki::scene::renameMaterial(model, findMaterial(materials, "stone"), "a b"));

    const toki::scene::Scene made = makeLaidOut(scene, model);
    const std::vector<SceneMaterial> after =
        toki::scene::listSceneMaterials(made, toki::scene::readSceneMesh(made, 0));
    REQUIRE(findMaterial(after, "moss").countFaces() == 2);
    REQUIRE(findMaterial(after, "brick").countFaces() == 0);
    REQUIRE(std::ranges::find(after, "grass", &SceneMaterial::name) == after.end());
}
