#include "n64/DisplayListWriter.h"

#include "DisplayListBuilder.h"
#include "base/Types.h"
#include "n64/FixedMatrix.h"
#include "n64/Memory.h"
#include "n64/Opcode.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <variant>
#include <vector>

using base::f32;
using base::s32;
using base::u32;
using base::u8;
using n64::test::DisplayListBuilder;

namespace {

constexpr u32 SegmentBase = 0x0010'0000;
constexpr s32 One = 0x0001'0000;
constexpr u8 Segment = 0x06;

std::vector<n64::Triangle> findTriangles(const n64::RspResult& result) {
    std::vector<n64::Triangle> triangles;
    for (const n64::RspOutput& output : result.outputs) {
        if (const auto* triangle = std::get_if<n64::Triangle>(&output)) {
            triangles.push_back(*triangle);
        }
    }
    return triangles;
}

} // namespace

TEST_CASE("a matrix converts from floats by truncating toward zero", "[n64][matrix]") {
    std::array<f32, 16> elements{};
    elements[0] = 1.5F;
    elements[1] = -0.25F;
    elements[2] = 1.0F / 3.0F;
    elements[3] = 1.0e9F;

    const n64::FixedMatrix matrix = n64::makeFixedMatrix(elements);

    REQUIRE(matrix.getElement(0, 0) == 0x0001'8000);
    REQUIRE(matrix.getElement(0, 1) == -0x0000'4000);
    REQUIRE(matrix.getElement(0, 2) == 0x0000'5555);
    REQUIRE(matrix.getElement(0, 3) == 0x7FFF'FFFF);
}

TEST_CASE("a written matrix reads back as itself", "[n64][matrix]") {
    const n64::FixedMatrix matrix{
        .elements = {One, -2, 3 * One, 4, -One, One / 2, 7, 8, 9, 10, One, 12, 13, -14, 15, One}};

    REQUIRE(n64::readFixedMatrix(n64::writeFixedMatrix(matrix)) == matrix);
}

TEST_CASE("a written viewport reads back as itself", "[n64][viewport]") {
    const n64::Viewport viewport{.scale = {640.0F, 360.25F, 511.0F},
                                 .translate = {640.0F, 360.5F, 511.0F}};

    REQUIRE(n64::readViewport(n64::writeViewport(viewport)) == viewport);
}

TEST_CASE("the writer's words are the words the test builder spells out", "[n64][writer]") {
    n64::DisplayListWriter writer(Segment);
    DisplayListBuilder builder;

    REQUIRE(writer.addMatrix(n64::makeIdentityMatrix()) ==
            builder.addMatrix({One, 0, 0, 0, 0, One, 0, 0, 0, 0, One, 0, 0, 0, 0, One}));
    REQUIRE(
        writer.addVertex({.position = {-5, 6, 7}, .texture = {32, -64}, .color = {1, 2, 3, 4}}) ==
        builder.addVertex({-5, 6, 7}, {32, -64}, {1, 2, 3, 4}));
    writer.loadMatrix(0x0600'0000, n64::MatrixLoad::Projection);
    builder.applyMatrix(0x0600'0000, 0x04 | 0x02);
    writer.loadMatrix(0x0600'0000, n64::MatrixLoad::ModelView);
    builder.applyMatrix(0x0600'0000, 0x02);
    writer.loadVertices(0x0600'0040, 4, 2);
    builder.loadVertices(0x0600'0040, 4, 2);
    writer.drawTriangle({0, 1, 2});
    builder.drawTriangle(0, 1, 2);
    writer.drawTriangles({0, 1, 2}, {3, 4, 5});
    builder.drawTriangles({0, 1, 2}, {3, 4, 5});
    writer.setGeometryMode(n64::geometry::CullBack, n64::geometry::Shade);
    builder.setGeometryMode(n64::geometry::CullBack, n64::geometry::Shade);
    writer.setViewport(0x0600'0080);
    builder.moveMemory(0x0600'0080, n64::ViewportSize, 0x08, 0);
    writer.callDisplayList(0x0600'0100);
    builder.callDisplayList(0x0600'0100);
    writer.endDisplayList();
    builder.endDisplayList();

    REQUIRE(writer.getBytes() == builder.getBytes());
}

TEST_CASE("what the writer writes, the RSP runs", "[n64][writer]") {
    n64::DisplayListWriter writer(Segment);
    const u32 viewport = writer.addViewport(n64::makeDefaultViewport());
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());
    const u32 vertices = writer.addVertex({.position = {0, 0, 0}, .color = {255, 0, 0, 255}});
    static_cast<void>(writer.addVertex({.position = {1, 0, 0}, .color = {255, 0, 0, 255}}));
    static_cast<void>(writer.addVertex({.position = {0, 1, 0}, .color = {255, 0, 0, 255}}));
    const u32 start = writer.setViewport(viewport);
    writer.loadMatrix(identity, n64::MatrixLoad::Projection);
    writer.loadMatrix(identity, n64::MatrixLoad::ModelView);
    writer.setGeometryMode(0x00FF'FFFF, n64::geometry::Shade);
    writer.loadVertices(vertices, 3, 0);
    writer.drawTriangle({0, 1, 2});
    writer.endDisplayList();

    n64::Memory memory;
    REQUIRE(memory.map(SegmentBase, writer.getBytes()));
    n64::RspTask task{.displayList = start};
    task.segments[Segment] = SegmentBase;
    const n64::RspResult result = n64::runTask(memory, task);

    REQUIRE(result.diagnostics.empty());
    const std::vector<n64::Triangle> triangles = findTriangles(result);
    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[1].position[0] == Catch::Approx(1.0F));
    REQUIRE(triangles[0].vertices[2].position[1] == Catch::Approx(1.0F));
    REQUIRE(triangles[0].vertices[0].color == std::array<u8, 4>{255, 0, 0, 255});
}
