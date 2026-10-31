#include "app/viewport/domain/TransformSession.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <string>
#include <vector>

using Catch::Approx;
using toki::viewport::AxisConstraint;
using toki::viewport::Snapping;
using toki::viewport::TransformKind;
using toki::viewport::TransformSession;

TEST_CASE("an axis key holds to that axis alone or frees it, with Shift to the other two",
          "[viewport][transform]") {
    AxisConstraint constraint;
    REQUIRE(constraint.isFree());
    REQUIRE(constraint.findRotationAxis() == 1);

    constraint.constrainTo(0);
    REQUIRE(constraint.axes == std::array{true, false, false});
    REQUIRE(constraint.findRotationAxis() == 0);
    constraint.constrainTo(0);
    REQUIRE(constraint.isFree());

    constraint.constrainToPlane(1);
    REQUIRE(constraint.axes == std::array{true, false, true});
    constraint.constrainToPlane(1);
    REQUIRE(constraint.isFree());
}

TEST_CASE("an axis key pressed twice holds to the normal, and a third time frees it",
          "[viewport][transform]") {
    const glm::vec3 normal = glm::normalize(glm::vec3(1.0F, 1.0F, 0.0F));
    AxisConstraint constraint;
    constraint.constrainTo(2, normal);
    REQUIRE_FALSE(constraint.basis);
    constraint.constrainTo(2, normal);
    REQUIRE(constraint.basis);
    REQUIRE(constraint.axes == std::array{false, false, true});
    REQUIRE(constraint.findDirection(2) == normal);
    REQUIRE(std::string(constraint.findName(2)) == "N");
    constraint.constrainTo(2, normal);
    REQUIRE(constraint.isFree());
    REQUIRE_FALSE(constraint.basis);

    constraint.constrainToPlane(0, normal);
    constraint.constrainToPlane(0, normal);
    REQUIRE(constraint.axes == std::array{false, true, true});
    REQUIRE(glm::dot(constraint.findDirection(1), normal) == Approx(0.0F).margin(1e-6));
    REQUIRE(glm::dot(constraint.findDirection(2), normal) == Approx(0.0F).margin(1e-6));

    AxisConstraint alongNormal;
    alongNormal.constrainTo(1, normal);
    alongNormal.constrainTo(1, normal);
    TransformSession session(TransformKind::Move);
    session.setDragged({3.0F, 1.0F, 5.0F});
    const glm::vec3 offset = session.makeResult(alongNormal, {}).offset;
    REQUIRE(offset.x == Approx(2.0F));
    REQUIRE(offset.y == Approx(2.0F));
    REQUIRE(offset.z == Approx(0.0F).margin(1e-6));
    REQUIRE(session.describe(alongNormal, {}) == "Move  N 2.8");

    const std::array<glm::vec3, 1> points{glm::vec3(1.0F, 0.0F, 0.0F)};
    TransformSession turn(TransformKind::Rotate);
    turn.setDraggedAngle(180.0F);
    const std::vector<glm::vec3> turned = toki::viewport::makeTransformedPoints(
        points, glm::vec3(0.0F), turn.makeResult(alongNormal, {}));
    REQUIRE(turned[0].x == Approx(0.0F).margin(1e-5));
    REQUIRE(turned[0].y == Approx(1.0F));
}

TEST_CASE("typed numbers take the drag's place, field by field", "[viewport][transform]") {
    TransformSession session(TransformKind::Move);
    session.setDragged({33.0F, 44.0F, 55.0F});
    AxisConstraint onX;
    onX.constrainTo(0);
    for (const char character : {'1', '2', '0', '-'}) {
        REQUIRE(session.type(character));
    }
    REQUIRE_FALSE(session.type('q'));
    REQUIRE(session.makeResult(onX, {}).offset == glm::vec3(-120.0F, 0.0F, 0.0F));
    REQUIRE(session.describe(onX, {}) == "Move  X [-120|]");

    TransformSession free(TransformKind::Move);
    REQUIRE(free.type('5'));
    free.moveToNextField({});
    REQUIRE(free.type('3'));
    free.erase();
    REQUIRE(free.type('7'));
    REQUIRE(free.makeResult({}, {}).offset == glm::vec3(5.0F, 7.0F, 0.0F));
}

TEST_CASE("the grid snaps a drag but never what was typed", "[viewport][transform]") {
    const Snapping grid{.isGridOn = true};
    TransformSession move(TransformKind::Move);
    move.setDragged({12.0F, 7.0F, 26.0F});
    REQUIRE(move.makeResult({}, grid).offset == glm::vec3(10.0F, 10.0F, 30.0F));
    AxisConstraint onY;
    onY.constrainTo(1);
    REQUIRE(move.makeResult(onY, grid).offset == glm::vec3(0.0F, 10.0F, 0.0F));

    TransformSession rotate(TransformKind::Rotate);
    rotate.setDraggedAngle(22.0F);
    REQUIRE(rotate.makeResult({}, grid).degrees == 15.0F);
    REQUIRE(rotate.makeResult({}, grid).axis == 1);
    REQUIRE(rotate.type('9'));
    REQUIRE(rotate.type('1'));
    REQUIRE(rotate.makeResult({}, grid).degrees == 91.0F);
}

TEST_CASE("a drag moves along the axis held, or in the plane facing the camera",
          "[viewport][transform]") {
    const glm::mat4 identity(1.0F);
    const glm::vec2 size{200.0F, 100.0F};
    AxisConstraint onX;
    onX.constrainTo(0);
    const glm::vec3 alongX =
        toki::viewport::findDragOffset(identity, size, {100.0F, 50.0F}, {150.0F, 25.0F},
                                       {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -5.0F}, onX);
    REQUIRE(alongX.x == Approx(0.5F));
    REQUIRE(alongX.y == Approx(0.0F));

    const glm::vec3 facing =
        toki::viewport::findDragOffset(identity, size, {100.0F, 50.0F}, {150.0F, 25.0F},
                                       {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -5.0F}, {});
    REQUIRE(facing.x == Approx(0.5F));
    REQUIRE(facing.y == Approx(0.5F));
    REQUIRE(facing.z == Approx(0.0F).margin(1e-5));
}

TEST_CASE("counterclockwise on the image turns about an axis pointing at the camera",
          "[viewport][transform]") {
    const glm::vec3 axis{0.0F, 0.0F, 1.0F};
    REQUIRE(toki::viewport::findDragAngle({0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, -10.0F}, axis,
                                          {0.0F, 0.0F, 5.0F}, {}) == Approx(90.0F));
    REQUIRE(toki::viewport::findDragAngle({0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, -10.0F}, axis,
                                          {0.0F, 0.0F, -5.0F}, {}) == Approx(-90.0F));
}

TEST_CASE("the ground is the highest surface below a point, within reach above it",
          "[viewport][transform]") {
    const std::array<glm::vec3, 6> triangles{
        glm::vec3{-100.0F, 0.0F, -100.0F}, glm::vec3{100.0F, 0.0F, -100.0F},
        glm::vec3{0.0F, 0.0F, 100.0F},     glm::vec3{-100.0F, 50.0F, -100.0F},
        glm::vec3{100.0F, 50.0F, -100.0F}, glm::vec3{0.0F, 50.0F, 100.0F}};
    REQUIRE(toki::viewport::findGroundBelow(triangles, {0.0F, 10.0F, 0.0F}, 20.0F) == 0.0F);
    REQUIRE(toki::viewport::findGroundBelow(triangles, {0.0F, 10.0F, 0.0F}, 50.0F) == 50.0F);
    REQUIRE_FALSE(toki::viewport::findGroundBelow(triangles, {500.0F, 10.0F, 0.0F}, 50.0F));
}

TEST_CASE("points are moved and set down on the ground, or turned about the pivot",
          "[viewport][transform]") {
    const std::array<glm::vec3, 2> points{glm::vec3{10.0F, 20.0F, 0.0F},
                                          glm::vec3{-10.0F, 20.0F, 0.0F}};
    const std::array<glm::vec3, 3> ground{glm::vec3{-100.0F, 5.0F, -100.0F},
                                          glm::vec3{100.0F, 5.0F, -100.0F},
                                          glm::vec3{0.0F, 5.0F, 100.0F}};
    const auto moved =
        toki::viewport::makeTransformedPoints(points, {}, {.offset = {0.0F, 10.0F, 0.0F}}, ground);
    REQUIRE(moved[0] == glm::vec3(10.0F, 5.0F, 0.0F));

    const auto turned = toki::viewport::makeTransformedPoints(
        points, {0.0F, 20.0F, 0.0F}, {.kind = TransformKind::Rotate, .axis = 1, .degrees = 90.0F});
    REQUIRE(turned[0].x == Approx(0.0F).margin(1e-4));
    REQUIRE(turned[0].z == Approx(-10.0F));
}

TEST_CASE("a scale stretches from the pivot along the axes held, by a drag or a typed factor",
          "[viewport][transform]") {
    TransformSession session(TransformKind::Scale);
    session.setDraggedFactor(glm::vec3(1.23F));
    AxisConstraint free;
    AxisConstraint alongX;
    alongX.constrainTo(0);

    REQUIRE(session.makeResult(free, {}).factor == glm::vec3(1.23F));
    REQUIRE(session.makeResult(alongX, {}).factor == glm::vec3(1.23F, 1.0F, 1.0F));
    REQUIRE(session.makeResult(free, {.isGridOn = true}).factor.y == Approx(1.2F));
    REQUIRE(session.describe(alongX, {}) == "Scale  X 1.23");

    REQUIRE(session.type('2'));
    REQUIRE(session.makeResult(free, {}).factor == glm::vec3(2.0F));

    const std::array<glm::vec3, 1> points{glm::vec3(3.0F, 1.0F, 1.0F)};
    const std::vector<glm::vec3> scaled = toki::viewport::makeTransformedPoints(
        points, glm::vec3(1.0F), session.makeResult(alongX, {}));
    REQUIRE(scaled[0] == glm::vec3(5.0F, 1.0F, 1.0F));
    REQUIRE(toki::viewport::findDragFactor({0.0F, 0.0F}, {10.0F, 0.0F}, {0.0F, 25.0F}) ==
            Approx(2.5F));
}
