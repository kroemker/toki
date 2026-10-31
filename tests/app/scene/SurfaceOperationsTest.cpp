#include "app/scene/domain/SurfaceOperations.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneSurface.h"
#include "app/scene/domain/SurfaceEdits.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <vector>

using base::u16;
using base::u64;
using base::usize;
using toki::scene::Collision;
using toki::scene::CollisionMaterial;
using toki::scene::MeshPick;
using toki::scene::RoomMesh;
using toki::scene::SceneMesh;

namespace {

/// A floor of three faces, the first two drawn under one material and solid
/// as type 1 and 2, the third drawn under another and not solid, and a wall
/// no face has, of type 1.
struct Floor {
    SceneMesh mesh;
    Collision collision;
    toki::scene::SceneSurface surface;
};

Floor makeFloor() {
    Floor floor;
    RoomMesh room{.room = 0};
    const std::vector<glm::vec3> positions{{0.0F, 0.0F, 0.0F},
                                           {100.0F, 0.0F, 0.0F},
                                           {0.0F, 0.0F, 100.0F},
                                           {100.0F, 0.0F, 100.0F},
                                           {200.0F, 0.0F, 0.0F}};
    for (usize vertex = 0; vertex < positions.size(); ++vertex) {
        room.vertices.push_back({.position = positions[vertex], .entries = {vertex}});
        room.entries.push_back({.key = {.symbol = "vtx", .index = vertex}});
        toki::scene::setVtxPosition(room.entries.back().vtx, glm::mat4(1.0F), positions[vertex]);
    }
    const std::array<std::array<usize, 3>, 3> faces{{{0, 2, 1}, {1, 2, 3}, {1, 3, 4}}};
    for (usize face = 0; face < faces.size(); ++face) {
        room.faces.push_back({.vertices = faces[face],
                              .corners = faces[face],
                              .material = {.content = face < 2 ? u64{7} : u64{9}}});
    }
    floor.mesh.rooms.push_back(room);
    floor.collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 0, 100}, {100, 50, 0}};
    floor.collision.polygons = {{.type = 1, .vertexData = {0, 2, 1}},
                                {.type = 2, .vertexData = {1, 2, 3}},
                                {.type = 1, .vertexData = {1, 3, 4}}};
    floor.surface = toki::scene::pairSurface(floor.mesh, floor.collision);
    floor.mesh = toki::scene::makeSolidMesh(floor.mesh, floor.surface, floor.collision);
    return floor;
}

} // namespace

TEST_CASE("a face made solid takes what its material's faces are made of, else what most are",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    REQUIRE_FALSE(floor.mesh.rooms[0].faces[2].collision);

    CHECK(toki::scene::findDefaultCollision(floor.mesh, floor.collision, {.content = 7}).type == 1);
    CHECK(toki::scene::findDefaultCollision(floor.mesh, floor.collision, {.content = 9}) ==
          CollisionMaterial{.type = 1});

    const std::array<MeshPick, 1> picks{{{.room = 0, .faces = {1, 2}}}};
    const std::vector<toki::scene::OperatedRoom> operated =
        toki::scene::makeFacesSolid(floor.mesh, floor.collision, picks);
    REQUIRE(operated.size() == 1);
    CHECK(operated[0].draft.faces[1].collision == CollisionMaterial{.type = 2});
    CHECK(operated[0].draft.faces[2].collision == CollisionMaterial{.type = 1});

    const std::array<toki::scene::RoomDraft, 1> drafts{operated[0].draft};
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr);
    CHECK(made.polygons.size() == 4);
}

TEST_CASE("faces set to no collision lose their polygons", "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<MeshPick, 1> picks{{{.room = 0, .faces = {0}}}};

    const std::vector<toki::scene::OperatedRoom> operated =
        toki::scene::setFacesCollision(floor.mesh, picks, std::nullopt);
    const std::array<toki::scene::RoomDraft, 1> drafts{operated[0].draft};
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr);

    REQUIRE(made.polygons.size() == 2);
    CHECK(made.polygons[0].type == 2);
}

TEST_CASE("a solid face drawn no more is a polygon no face has, made of what it was",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<MeshPick, 1> picks{{{.room = 0, .faces = {1, 2}}}};

    const std::vector<toki::scene::OperatedRoom> operated = toki::scene::undrawFaces(
        floor.mesh, picks, toki::scene::makeSolidOnlyDraft(floor.collision, floor.surface, 1));

    REQUIRE(operated.size() == 2);
    CHECK(operated[0].draft.faces.size() == 2);
    const toki::scene::RoomDraft& solidOnly = operated[1].draft;
    REQUIRE(solidOnly.faces.size() == 2);
    CHECK(solidOnly.faces[1].collision == CollisionMaterial{.type = 2});
    CHECK(operated[1].faces == std::vector<usize>{1});

    const std::array<toki::scene::RoomDraft, 1> drafts{operated[0].draft};
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, &solidOnly);
    REQUIRE(made.polygons.size() == 3);
    CHECK(made.polygons.back().type == 2);
    const toki::scene::SceneSurface paired = toki::scene::pairSurface(SceneMesh{}, made);
    CHECK(toki::scene::listSolidOnlyPolygons(paired).size() == 3);
}

TEST_CASE("polygons no face has are given a material, or taken out without one",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<usize, 1> wall{0};

    const toki::scene::OperatedRoom given = toki::scene::setSolidOnlyCollision(
        toki::scene::makeSolidOnlyDraft(floor.collision, floor.surface, 1), wall,
        CollisionMaterial{.type = 0, .flags = 0b0010});
    CHECK(toki::scene::makeDraftedCollision(floor.collision, floor.surface, {}, &given.draft)
              .polygons[2]
              .type == 0);

    const toki::scene::OperatedRoom taken = toki::scene::setSolidOnlyCollision(
        toki::scene::makeSolidOnlyDraft(floor.collision, floor.surface, 1), wall, std::nullopt);
    CHECK(toki::scene::makeDraftedCollision(floor.collision, floor.surface, {}, &taken.draft)
              .polygons.size() == 2);
}

TEST_CASE("a face whose corners are in a line at whole units is never made solid",
          "[scene][surface]") {
    Floor floor;
    RoomMesh room{.room = 0};
    const std::vector<glm::vec3> positions{{0.0F, 0.0F, 0.0F},
                                           {100.0F, 0.0F, 0.0F},
                                           {0.0F, 0.0F, 100.0F},
                                           {100.0F, 0.0F, 100.0F},
                                           {50.2F, 0.0F, 0.3F}};
    for (usize vertex = 0; vertex < positions.size(); ++vertex) {
        room.vertices.push_back({.position = positions[vertex], .entries = {vertex}});
        room.entries.push_back({.key = {.symbol = "vtx", .index = vertex}});
        toki::scene::setVtxPosition(room.entries.back().vtx, glm::mat4(1.0F), positions[vertex]);
    }
    for (const std::array<usize, 3>& face :
         std::array<std::array<usize, 3>, 3>{{{0, 2, 1}, {1, 2, 3}, {0, 4, 1}}}) {
        room.faces.push_back({.vertices = face, .corners = face, .material = {.content = 7}});
    }
    floor.mesh.rooms.push_back(room);
    floor.collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 0, 100}};
    floor.collision.polygons = {{.type = 1, .vertexData = {0, 2, 1}},
                                {.type = 1, .vertexData = {1, 2, 3}}};
    floor.surface = toki::scene::pairSurface(floor.mesh, floor.collision);
    floor.mesh = toki::scene::makeSolidMesh(floor.mesh, floor.surface, floor.collision);
    const std::vector<toki::scene::SceneMaterial> materials{{.name = "Untextured", .content = 7}};
    const CollisionMaterial wood{.type = 3};

    CHECK(toki::scene::canFaceBeSolid(floor.mesh.rooms[0], 0));
    CHECK_FALSE(toki::scene::canFaceBeSolid(floor.mesh.rooms[0], 2));
    CHECK_FALSE(toki::scene::isCollisionMixed(floor.mesh, materials, 0));
    CHECK(toki::scene::countUnbound(floor.mesh, materials, 0, wood) == 2);
    CHECK(toki::scene::countUnbound(floor.mesh, materials, 0, std::nullopt) == 2);

    const std::vector<toki::scene::OperatedRoom> bound =
        toki::scene::bindFaces(floor.mesh, materials, 0, wood);
    REQUIRE(bound.size() == 1);
    CHECK(bound[0].draft.faces[0].collision == wood);
    CHECK_FALSE(bound[0].draft.faces[2].collision);
    const std::array<toki::scene::RoomDraft, 1> drafts{bound[0].draft};
    CHECK(toki::scene::countLeftOutPolygons(floor.collision, floor.surface, drafts, nullptr) == 0);
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr);
    const SceneMesh reread =
        toki::scene::makeSolidMesh(floor.mesh, toki::scene::pairSurface(floor.mesh, made), made);
    CHECK(toki::scene::countUnbound(reread, materials, 0, wood) == 0);

    const std::array<MeshPick, 1> flat{MeshPick{.room = 0, .faces = {2}}};
    CHECK_FALSE(toki::scene::setFacesCollision(floor.mesh, flat, wood)[0].draft.faces[2].collision);
    CHECK_FALSE(
        toki::scene::makeFacesSolid(floor.mesh, floor.collision, flat)[0].draft.faces[2].collision);
}
