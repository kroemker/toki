#include "app/scene/domain/MaterialBindings.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MaterialBindingsFile.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SurfaceOperations.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using base::usize;
using toki::scene::Collision;
using toki::scene::CollisionMaterial;
using toki::scene::MaterialBinding;
using toki::scene::MaterialBindings;
using toki::scene::SceneMaterial;
using toki::scene::SurfaceType;

namespace {

Collision makeCollision() {
    Collision collision;
    collision.surfaceTypes = {{.data = {0x00000000, 0x000007C0}},
                              {.data = {0x00200000, 0x000007C2}}};
    return collision;
}

/// A room of three faces, two drawn under the material of content 7 and one
/// under that of 9, of which the first is solid of surface type 1.
toki::scene::SceneMesh makeMesh() {
    toki::scene::RoomMesh room{.room = 0};
    for (const glm::vec3& position :
         {glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(100.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, 100.0F),
          glm::vec3(100.0F, 0.0F, 100.0F), glm::vec3(200.0F, 0.0F, 0.0F)}) {
        room.vertices.push_back({.position = position, .entries = {room.entries.size()}});
        room.entries.push_back({.key = {.symbol = "vtx", .index = room.entries.size()}});
    }
    const std::array<std::array<usize, 3>, 3> corners{{{0, 2, 1}, {1, 2, 3}, {1, 3, 4}}};
    for (usize face = 0; face < 3; ++face) {
        room.faces.push_back(
            {.vertices = corners[face],
             .corners = corners[face],
             .material = face < 2 ? toki::scene::MeshMaterial{.content = 7, .texture = "Grass"}
                                  : toki::scene::MeshMaterial{.content = 9, .texture = "Rock"}});
    }
    room.faces[0].collision = CollisionMaterial{.type = 1};
    toki::scene::SceneMesh mesh;
    mesh.rooms.push_back(room);
    return mesh;
}

std::vector<SceneMaterial> makeMaterials() {
    return {{.name = "Grass", .content = 7}, {.name = "Rock", .content = 9}};
}

} // namespace

TEST_CASE("material bindings read back from the text they are written as", "[scene][surface]") {
    MaterialBindings bindings;
    bindings.materials["grass"] = {
        .isSolid = true,
        .surface = {.data = {0x00200000, 0x000007C2}},
        .flags = toki::scene::setMaterialFlag(CollisionMaterial{},
                                              toki::scene::PolygonFlag::IgnoreCamera, true)
                     .flags};
    bindings.materials["00000000DEADBEEF"] = {};

    const std::string text = toki::scene::formatMaterialBindings(bindings);
    const toki::scene::ReadBindings read = toki::scene::parseMaterialBindings(text, "scene.toml");

    CHECK(read.diagnostics.empty());
    CHECK(read.bindings == bindings);
    CHECK(text.find("ignore_camera") != std::string::npos);
}

TEST_CASE("a binding file that cannot be read is a diagnostic, and the rest is kept",
          "[scene][surface]") {
    const toki::scene::ReadBindings read = toki::scene::parseMaterialBindings(
        "[materials.a]\nsolid = true\nsurface = [1]\n[materials.b]\nsolid = false\n", "scene.toml");

    REQUIRE(read.diagnostics.size() == 1);
    CHECK(read.diagnostics[0].location.line == 1);
    CHECK(read.bindings.materials.size() == 1);
    CHECK(read.bindings.materials.contains("b"));
    CHECK(toki::scene::parseMaterialBindings("[", "scene.toml").diagnostics.size() == 1);
    CHECK(toki::scene::makeSceneFilePath("root", "spot00") ==
          std::filesystem::path("root") / ".toki" / "scenes" / "spot00.toml");
}

TEST_CASE("a binding names the surface type by its words, found again in any collision",
          "[scene][surface]") {
    const Collision collision = makeCollision();
    const MaterialBinding binding =
        toki::scene::makeMaterialBinding(collision, CollisionMaterial{.type = 1, .flags = 2});

    CHECK(binding.surface == collision.surfaceTypes[1]);
    Collision reordered;
    reordered.surfaceTypes = {collision.surfaceTypes[1]};
    CHECK(toki::scene::findBoundCollision(reordered, binding).value() ==
          CollisionMaterial{.type = 0, .flags = 2});
    CHECK_FALSE(toki::scene::findBoundCollision(Collision{}, binding));
    CHECK(toki::scene::findBoundCollision(Collision{}, MaterialBinding{}).value() == std::nullopt);
}

TEST_CASE("bindings follow a surface type whose settings change", "[scene][surface]") {
    MaterialBindings bindings;
    const SurfaceType from{.data = {1, 2}};
    const SurfaceType to{.data = {3, 4}};
    bindings.materials["a"] = {.isSolid = true, .surface = from};
    bindings.materials["b"] = {.isSolid = true, .surface = from, .flags = 1};

    const MaterialBindings rebound = toki::scene::makeReboundSurface(bindings, from, to, 1);

    CHECK(rebound.materials.at("a").surface == from);
    CHECK(rebound.materials.at("b").surface == to);
    CHECK(toki::scene::makeReboundSurface(bindings, from, to, std::nullopt)
              .materials.at("a")
              .surface == to);
}

TEST_CASE("binding a material makes every face drawn under it what the binding says",
          "[scene][surface]") {
    const toki::scene::SceneMesh mesh = makeMesh();
    const std::vector<SceneMaterial> materials = makeMaterials();
    const Collision collision = makeCollision();
    MaterialBindings bindings;
    bindings.materials[toki::scene::makeMaterialKey(materials[0])] =
        toki::scene::makeMaterialBinding(collision, CollisionMaterial{.type = 1});

    CHECK(toki::scene::isCollisionMixed(mesh, materials, 0));
    CHECK_FALSE(toki::scene::isCollisionMixed(mesh, materials, 1));
    const std::vector<std::optional<toki::scene::ResolvedBinding>> resolved =
        toki::scene::resolveMaterialBindings(bindings, collision, mesh, materials);
    REQUIRE(resolved.size() == 2);
    REQUIRE(resolved[0]);
    CHECK(resolved[0]->collision.value() == CollisionMaterial{.type = 1});
    CHECK(resolved[0]->unbound == 1);
    CHECK_FALSE(resolved[1]);

    const std::vector<toki::scene::OperatedRoom> operated =
        toki::scene::bindFaces(mesh, materials, 0, CollisionMaterial{.type = 1});
    REQUIRE(operated.size() == 1);
    CHECK(operated[0].draft.faces[1].collision == CollisionMaterial{.type = 1});
    CHECK_FALSE(operated[0].draft.faces[2].collision);
    CHECK(toki::scene::bindFaces(mesh, materials, 1, std::nullopt).empty());

    const std::vector<toki::scene::OperatedRoom> assigned = toki::scene::makeBoundFaces(
        {toki::scene::OperatedRoom{.draft = operated[0].draft, .faces = {2}}}, resolved[0]);
    CHECK(assigned[0].draft.faces[2].collision == CollisionMaterial{.type = 1});
    CHECK_FALSE(toki::scene::makeBoundFaces(operated, resolved[1])[0].draft.faces[2].collision);
}

TEST_CASE("renaming a material moves its binding", "[scene][surface]") {
    MaterialBindings bindings;
    bindings.materials["stone"] = toki::scene::MaterialBinding{.isSolid = true, .flags = 2};

    const MaterialBindings renamed = toki::scene::makeRenamedBindings(bindings, "stone", "brick");
    CHECK_FALSE(renamed.materials.contains("stone"));
    CHECK(renamed.materials.at("brick").flags == 2);
    CHECK(toki::scene::makeRenamedBindings(bindings, "grass", "moss") == bindings);
}
