#include "app/scene/domain/ComponentSelection.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/EditMesh.h"
#include "app/viewport/domain/ViewportTools.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <set>
#include <vector>

using base::u16;
using base::usize;
using toki::scene::ComponentRef;
using toki::viewport::PickMode;
using toki::viewport::SelectionMode;

namespace {

/// Two triangles of a unit square on the floor, sharing the edge from 0 to 2.
toki::scene::EditMesh makeSquare() {
    toki::scene::Collision collision;
    collision.vertices = {{0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, 0, 100}};
    const auto makePolygon = [](const u16 a, const u16 b, const u16 c) {
        return toki::scene::CollisionPolygon{.vertexData = {a, b, c}};
    };
    collision.polygons = {makePolygon(0, 1, 2), makePolygon(0, 2, 3)};
    return toki::scene::makeCollisionEdit(collision);
}

} // namespace

TEST_CASE("a collision is one piece of faces over its vertices, each edge once", "[scene][mesh]") {
    const toki::scene::EditMesh mesh = makeSquare();

    REQUIRE(mesh.pieces.size() == 1);
    REQUIRE(mesh.pieces[0].faces.size() == 2);
    REQUIRE(mesh.pieces[0].edges.size() == 5);
    REQUIRE(toki::scene::findFaceMiddle(mesh.pieces[0], 0) ==
            glm::vec3(200.0F / 3.0F, 0.0F, 100.0F / 3.0F));
}

TEST_CASE("a ray from above picks the face it meets first", "[scene][mesh]") {
    const toki::scene::EditMesh mesh = makeSquare();

    const std::optional<ComponentRef> hit =
        toki::scene::findHitFace(mesh, {10.0F, 50.0F, 80.0F}, {0.0F, -1.0F, 0.0F}, {});
    const std::optional<ComponentRef> missed =
        toki::scene::findHitFace(mesh, {150.0F, 50.0F, 80.0F}, {0.0F, -1.0F, 0.0F}, {});
    const std::optional<ComponentRef> hidden =
        toki::scene::findHitFace(mesh, {10.0F, 50.0F, 80.0F}, {0.0F, -1.0F, 0.0F}, {false});

    REQUIRE(hit == ComponentRef{.piece = 0, .index = 1});
    REQUIRE_FALSE(missed);
    REQUIRE_FALSE(hidden);
}

TEST_CASE("a selection carries over between modes as Blender carries it", "[scene][mesh]") {
    const toki::scene::EditMesh mesh = makeSquare();
    toki::scene::ComponentSelection selection;
    const std::array<ComponentRef, 1> face{{{.piece = 0, .index = 0}}};
    selection.pick(face, PickMode::Replace);

    selection.setMode(SelectionMode::Vertex, mesh);
    REQUIRE(selection.getComponents() == std::set<ComponentRef>{{0, 0}, {0, 1}, {0, 2}});

    selection.setMode(SelectionMode::Edge, mesh);
    REQUIRE(selection.getComponents().size() == 3);

    const std::array<ComponentRef, 1> corner{{{.piece = 0, .index = 3}}};
    selection.setMode(SelectionMode::Vertex, mesh);
    selection.pick(corner, PickMode::Add);
    selection.setMode(SelectionMode::Face, mesh);
    REQUIRE(selection.getComponents() == std::set<ComponentRef>{{0, 0}, {0, 1}});
    REQUIRE(selection.listVertices(mesh).size() == 4);

    selection.pick(face, PickMode::Remove);
    REQUIRE(selection.getComponents() == std::set<ComponentRef>{{0, 1}});
}
