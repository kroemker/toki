#include "app/scene/domain/SceneSurface.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/RoomMesh.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <vector>

using base::s16;
using base::u16;
using base::usize;
using toki::scene::Collision;
using toki::scene::CollisionPolygon;
using toki::scene::MeshFaceRef;
using toki::scene::MeshVertexRef;
using toki::scene::RoomMesh;
using toki::scene::SceneMesh;

namespace {

RoomMesh makeRoom(const usize room, const std::vector<glm::vec3>& positions,
                  const std::vector<std::array<usize, 3>>& faces) {
    RoomMesh mesh{.room = room};
    for (const glm::vec3& position : positions) {
        mesh.vertices.push_back({.position = position});
    }
    for (const std::array<usize, 3>& face : faces) {
        mesh.faces.push_back({.vertices = face});
    }
    return mesh;
}

CollisionPolygon makePolygon(const u16 a, const u16 b, const u16 c, const u16 flags = 0) {
    return {.vertexData = {static_cast<u16>(a | flags), b, c}};
}

} // namespace

TEST_CASE("a polygon at a face's corners is that face's, in any order of corners",
          "[scene][surface]") {
    SceneMesh mesh;
    mesh.rooms.push_back(makeRoom(
        0, {{0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}, {100.0F, 0.0F, 100.0F}},
        {{0, 1, 2}, {1, 3, 2}}));
    Collision collision;
    collision.vertices = {{100, 0, 100}, {0, 0, 100}, {100, 0, 0}, {0, 50, 0}};
    collision.polygons = {makePolygon(0, 1, 2, 0x2000), makePolygon(1, 2, 3)};

    const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, collision);

    CHECK(surface.polygonFaces[0] == MeshFaceRef{.room = 0, .face = 1});
    CHECK_FALSE(surface.polygonFaces[1]);
    CHECK(toki::scene::findFacePolygon(surface, {.room = 0, .face = 1}) == 0);
    CHECK_FALSE(toki::scene::findFacePolygon(surface, {.room = 0, .face = 0}));
    CHECK(toki::scene::listSolidOnlyPolygons(surface) == std::vector<usize>{1});
    CHECK(surface.welds[0] == std::vector<MeshVertexRef>{{.room = 0, .vertex = 3}});
    CHECK(surface.welds[3].empty());
}

TEST_CASE("positions pair by the whole units collision stores them in", "[scene][surface]") {
    SceneMesh mesh;
    mesh.rooms.push_back(
        makeRoom(2, {{0.4F, 0.0F, 0.0F}, {99.6F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.2F}}, {{0, 1, 2}}));
    Collision collision;
    collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}};
    collision.polygons = {makePolygon(0, 1, 2)};

    const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, collision);

    CHECK(surface.polygonFaces[0] == MeshFaceRef{.room = 2, .face = 0});
    REQUIRE(surface.facePolygons.size() == 3);
    CHECK(surface.facePolygons[0].empty());
}

TEST_CASE("faces at one place pair with one polygon each, and a vertex welds in every room",
          "[scene][surface]") {
    const std::vector<glm::vec3> corners{
        {0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}};
    SceneMesh mesh;
    mesh.rooms.push_back(makeRoom(0, corners, {{0, 1, 2}}));
    mesh.rooms.push_back(makeRoom(1, corners, {{2, 1, 0}}));
    Collision collision;
    collision.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}};
    collision.polygons = {makePolygon(0, 1, 2), makePolygon(0, 2, 1), makePolygon(1, 2, 0)};

    const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, collision);

    CHECK(surface.polygonFaces[0] == MeshFaceRef{.room = 0, .face = 0});
    CHECK(surface.polygonFaces[1] == MeshFaceRef{.room = 1, .face = 0});
    CHECK_FALSE(surface.polygonFaces[2]);
    CHECK(surface.welds[1] == std::vector<MeshVertexRef>{{.room = 0, .vertex = 1},
                                                         {.room = 1, .vertex = 1}});
}

TEST_CASE("a polygon naming a vertex the collision does not hold is solid only",
          "[scene][surface]") {
    SceneMesh mesh;
    mesh.rooms.push_back(makeRoom(
        0, {{0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F}}, {{0, 1, 2}}));
    Collision collision;
    collision.vertices = {{0, 0, 0}, {100, 0, 0}};
    collision.polygons = {makePolygon(0, 1, 7)};

    const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, collision);

    CHECK_FALSE(surface.polygonFaces[0]);
}
