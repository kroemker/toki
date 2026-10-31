#include "app/scene/domain/SurfaceEdits.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneSurface.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

using base::s16;
using base::u16;
using base::usize;
using toki::scene::Collision;
using toki::scene::CollisionMaterial;
using toki::scene::CollisionPolygon;
using toki::scene::RoomMesh;
using toki::scene::SceneMesh;

namespace {

using Point = std::array<s16, 3>;

RoomMesh makeRoom(const usize room, const std::vector<glm::vec3>& positions,
                  const std::vector<std::array<usize, 3>>& faces) {
    RoomMesh mesh{.room = room};
    for (usize vertex = 0; vertex < positions.size(); ++vertex) {
        mesh.vertices.push_back({.position = positions[vertex], .entries = {vertex}});
        mesh.entries.push_back({.key = {.symbol = "vtx", .index = vertex}});
        toki::scene::setVtxPosition(mesh.entries.back().vtx, glm::mat4(1.0F), positions[vertex]);
    }
    for (const std::array<usize, 3>& face : faces) {
        mesh.faces.push_back({.vertices = face, .corners = face});
    }
    return mesh;
}

CollisionPolygon makePolygon(const u16 a, const u16 b, const u16 c, const u16 type = 0) {
    return {.type = type, .vertexData = {a, b, c}};
}

std::array<Point, 3> findCorners(const Collision& collision, const CollisionPolygon& polygon) {
    std::array<Point, 3> corners{};
    for (usize corner = 0; corner < 3; ++corner) {
        corners[corner] = collision.vertices[toki::scene::getVertexIndex(polygon, corner)];
    }
    return corners;
}

/// A floor of two faces, each paired with a polygon, and a wall above its
/// far edge that no face has.
struct Floor {
    SceneMesh mesh;
    Collision collision;
    toki::scene::SceneSurface surface;
};

Floor makeFloor() {
    Floor floor;
    floor.mesh.rooms.push_back(makeRoom(
        0, {{0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}, {100.0F, 0.0F, 100.0F}},
        {{0, 2, 1}, {1, 2, 3}}));
    floor.collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 0, 100}, {100, 50, 0}};
    floor.collision.polygons = {makePolygon(0, 2, 1, 1), makePolygon(1, 2, 3, 2),
                                makePolygon(1, 3, 4, 3)};
    floor.surface = toki::scene::pairSurface(floor.mesh, floor.collision);
    floor.mesh = toki::scene::makeSolidMesh(floor.mesh, floor.surface, floor.collision);
    return floor;
}

} // namespace

TEST_CASE("a solid face is made of its polygon's material, and its drafts keep it",
          "[scene][surface]") {
    const Floor floor = makeFloor();

    REQUIRE(floor.mesh.rooms[0].faces[0].collision == CollisionMaterial{.type = 1});
    REQUIRE(floor.mesh.rooms[0].faces[1].collision == CollisionMaterial{.type = 2});
    const toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(floor.mesh.rooms[0]);
    CHECK(draft.faces[1].collision == CollisionMaterial{.type = 2});
}

TEST_CASE("drafts that change nothing leave the collision as it was", "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<toki::scene::RoomDraft, 1> drafts{
        toki::scene::makeRoomDraft(floor.mesh.rooms[0])};
    const toki::scene::RoomDraft solidOnly =
        toki::scene::makeSolidOnlyDraft(floor.collision, floor.surface, 1);

    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, &solidOnly);

    CHECK(made.vertices == floor.collision.vertices);
    CHECK(made.polygons == floor.collision.polygons);
}

TEST_CASE("a face taken out takes its polygon, and a face grown from a solid one is solid",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(floor.mesh.rooms[0]);
    const std::array<usize, 1> second{1};
    static_cast<void>(toki::scene::insetFaces(draft, second));
    const std::array<usize, 1> first{0};
    toki::scene::deleteFaces(draft, first);

    const std::array<toki::scene::RoomDraft, 1> drafts{draft};
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr);

    REQUIRE(made.polygons.size() == draft.faces.size() + 1);
    CHECK(made.polygons[0].type == 3);
    CHECK(findCorners(made, made.polygons[0]) ==
          findCorners(floor.collision, floor.collision.polygons[2]));
    for (usize polygon = 1; polygon < made.polygons.size(); ++polygon) {
        CHECK(made.polygons[polygon].type == 2);
    }
}

TEST_CASE("a face with its collision taken away is no polygon, the mesh as it was",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(floor.mesh.rooms[0]);
    draft.faces[0].collision.reset();
    draft.faces[1].collision = CollisionMaterial{.type = 0, .flags = 0b0001};

    const std::array<toki::scene::RoomDraft, 1> drafts{draft};
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr);

    REQUIRE(made.polygons.size() == 2);
    CHECK(made.polygons[0].type == 0);
    CHECK(toki::scene::hasPolygonFlag(made.polygons[0], toki::scene::PolygonFlag::IgnoreCamera));
    CHECK(made.polygons[1].type == 3);
    CHECK(findCorners(made, made.polygons[1]) ==
          findCorners(floor.collision, floor.collision.polygons[2]));
}

TEST_CASE("polygons no face has are edited as a draft of their own", "[scene][surface]") {
    const Floor floor = makeFloor();
    toki::scene::RoomDraft solidOnly =
        toki::scene::makeSolidOnlyDraft(floor.collision, floor.surface, 1);
    REQUIRE(solidOnly.faces.size() == 1);
    REQUIRE(solidOnly.faces[0].original == 2);

    const std::optional<toki::scene::OperatedRoom> operated = toki::scene::operateDraft(
        solidOnly, toki::scene::MeshOperation::Delete, {.room = 1, .touchedFaces = {0}});
    REQUIRE(operated);
    const Collision made =
        toki::scene::makeDraftedCollision(floor.collision, floor.surface, {}, &operated->draft);

    REQUIRE(made.polygons.size() == 2);
    CHECK(made.vertices.size() == 4);
}

TEST_CASE("a collision vertex welded to a mesh vertex moved follows it, with the polygons "
          "no face has",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<toki::scene::MeshVertexMove, 1> moves{
        {{.room = 0, .vertex = 1, .position = {100.0F, 10.0F, 0.0F}}}};

    const Collision moved =
        toki::scene::makeWeldedCollision(floor.collision, floor.surface, floor.mesh, moves, {});

    REQUIRE(moved.vertices.size() == floor.collision.vertices.size());
    CHECK(moved.vertices[1] == Point{100, 10, 0});
    CHECK(moved.polygons[0].normal != floor.collision.polygons[0].normal);
    CHECK(moved.maxBounds == Point{100, 50, 100});
}

TEST_CASE("a collision vertex two rooms meet at is split when only one room's vertex moves",
          "[scene][surface]") {
    const std::vector<glm::vec3> corners{
        {0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}, {100.0F, 0.0F, 100.0F}};
    SceneMesh mesh;
    mesh.rooms.push_back(makeRoom(0, corners, {{0, 2, 1}}));
    mesh.rooms.push_back(makeRoom(1, corners, {{1, 2, 3}}));
    Collision collision;
    collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 0, 100}};
    collision.polygons = {makePolygon(0, 2, 1), makePolygon(1, 2, 3)};
    const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, collision);
    const std::array<toki::scene::MeshVertexMove, 1> moves{
        {{.room = 0, .vertex = 1, .position = {100.0F, 20.0F, 0.0F}}}};

    const Collision moved = toki::scene::makeWeldedCollision(collision, surface, mesh, moves, {});

    REQUIRE(moved.vertices.size() == 5);
    CHECK(moved.vertices[1] == Point{100, 0, 0});
    CHECK(moved.vertices[4] == Point{100, 20, 0});
    CHECK(toki::scene::getVertexIndex(moved.polygons[0], 2) == 4);
    CHECK(toki::scene::getVertexIndex(moved.polygons[1], 0) == 1);
}

TEST_CASE("mesh vertices welded to a collision vertex moved follow it", "[scene][surface]") {
    const Floor floor = makeFloor();
    const std::array<toki::scene::CollisionVertexMove, 1> moves{
        {{.vertex = 3, .position = {120, 0, 100}}}};

    const std::vector<toki::scene::MeshVertexMove> welded =
        toki::scene::findWeldedMeshMoves(floor.surface, moves);

    REQUIRE(welded.size() == 1);
    CHECK(welded[0].room == 0);
    CHECK(welded[0].vertex == 3);
    CHECK(welded[0].position == glm::vec3(120.0F, 0.0F, 100.0F));
    const Collision moved =
        toki::scene::makeWeldedCollision(floor.collision, floor.surface, floor.mesh, welded, moves);
    CHECK(moved.vertices[3] == Point{120, 0, 100});
    CHECK(moved.vertices.size() == floor.collision.vertices.size());
}

TEST_CASE("the sides an extrusion grows are flat until moved, and solid once moved apart",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(floor.mesh.rooms[0]);
    const std::array<usize, 1> second{1};
    const std::vector<usize> capped = toki::scene::extrudeFaces(draft, second);
    REQUIRE(capped.size() == 1);

    const std::vector<usize> flat = toki::scene::listFlatSolidFaces(draft);
    REQUIRE(flat.size() == 6);
    CHECK(std::ranges::find(flat, capped[0]) == flat.end());

    for (toki::scene::DraftFace& face : draft.faces) {
        face.original.reset();
    }
    for (const toki::scene::DraftCorner& corner : draft.faces[capped[0]].corners) {
        draft.vertices[corner.vertex].position.y += 40.0F;
        draft.vertices[corner.vertex].isMoved = true;
    }
    CHECK(toki::scene::listFlatSolidFaces(draft).empty());
    const std::array<toki::scene::RoomDraft, 1> drafts{draft};
    CHECK(toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr)
              .polygons.size() == draft.faces.size() + 1);
}

TEST_CASE("a solid face with its corners in a line is counted as left without a polygon",
          "[scene][surface]") {
    const Floor floor = makeFloor();
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(floor.mesh.rooms[0]);
    draft.vertices[2] = {.position = {50.0F, 0.0F, 0.0F}, .isMoved = true};

    const std::array<toki::scene::RoomDraft, 1> drafts{draft};
    CHECK(toki::scene::countLeftOutPolygons(floor.collision, floor.surface, drafts, nullptr) == 1);
    CHECK(toki::scene::makeDraftedCollision(floor.collision, floor.surface, drafts, nullptr)
              .polygons.size() == 2);

    const std::array<toki::scene::RoomDraft, 1> unchanged{
        toki::scene::makeRoomDraft(floor.mesh.rooms[0])};
    CHECK(toki::scene::countLeftOutPolygons(floor.collision, floor.surface, unchanged, nullptr) ==
          0);
}
