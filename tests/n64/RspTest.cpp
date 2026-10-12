#include "n64/Rsp.h"

#include "DisplayListBuilder.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "n64/Memory.h"
#include "n64/RspResult.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <variant>
#include <vector>

using base::s32;
using base::u32;
using base::u8;
using n64::test::DisplayListBuilder;

namespace {

constexpr u32 SegmentBase = 0x0010'0000;
constexpr s32 One = 0x0001'0000;
constexpr u32 Smooth = 0x0020'0000;
constexpr u32 Shade = 0x0000'0004;
constexpr u32 CullBack = 0x0000'0400;
constexpr u32 CullFront = 0x0000'0200;
constexpr u32 Lighting = 0x0002'0000;
constexpr u32 Fog = 0x0001'0000;
constexpr u32 Projection = 0x04;
constexpr u32 Load = 0x02;
constexpr u32 Push = 0x01;

n64::RspResult runDisplayList(const DisplayListBuilder& builder, const u32 start) {
    n64::Memory memory;
    REQUIRE(memory.map(SegmentBase, builder.getBytes()));
    n64::RspTask task{.displayList = start};
    task.segments[DisplayListBuilder::Segment] = SegmentBase;
    return n64::runTask(memory, task);
}

std::vector<n64::Triangle> findTriangles(const n64::RspResult& result) {
    std::vector<n64::Triangle> triangles;
    for (const n64::RspOutput& output : result.outputs) {
        if (const auto* triangle = std::get_if<n64::Triangle>(&output)) {
            triangles.push_back(*triangle);
        }
    }
    return triangles;
}

std::vector<n64::RdpCommand> findRdpCommands(const n64::RspResult& result) {
    std::vector<n64::RdpCommand> commands;
    for (const n64::RspOutput& output : result.outputs) {
        if (const auto* command = std::get_if<n64::RdpCommand>(&output)) {
            commands.push_back(*command);
        }
    }
    return commands;
}

std::array<s32, 16> makeTransform(const s32 scale, const std::array<s32, 3> translation) {
    return {
        scale,          0,  0, 0, 0, scale, 0, 0, 0, 0, scale, 0, translation[0], translation[1],
        translation[2], One};
}

u32 addTriangleVertices(DisplayListBuilder& builder) {
    const u32 address = builder.addVertex({0, 0, 0}, {0, 0}, {255, 0, 0, 255});
    builder.addVertex({10, 0, 0}, {0, 0}, {0, 255, 0, 255});
    builder.addVertex({0, 10, 0}, {0, 0}, {0, 0, 255, 255});
    return address;
}

} // namespace

TEST_CASE("an empty display list produces nothing", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.outputs.empty());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("vertices go through the modelview, then the projection", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 modelview = builder.addMatrix(makeTransform(One, {One, 2 * One, 3 * One}));
    const u32 projection = builder.addMatrix(makeTransform(2 * One, {0, 0, 0}));
    const u32 vertices = builder.addVertex({10, 20, 30}, {0, 0}, {1, 2, 3, 4});
    builder.addVertex({0, 0, 0}, {0, 0}, {0, 0, 0, 0});
    builder.addVertex({0, 0, 0}, {0, 0}, {0, 0, 0, 0});
    const u32 start = builder.applyMatrix(projection, Projection | Load);
    builder.applyMatrix(modelview, Load);
    builder.setGeometryMode(0, Smooth);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    const std::vector<n64::Triangle> triangles = findTriangles(result);
    REQUIRE(result.diagnostics.empty());
    REQUIRE(triangles.size() == 1);
    const auto& position = triangles[0].vertices[0].position;
    REQUIRE(position[0] == Catch::Approx(22.0F));
    REQUIRE(position[1] == Catch::Approx(44.0F));
    REQUIRE(position[2] == Catch::Approx(66.0F));
    REQUIRE(position[3] == Catch::Approx(1.0F));
    REQUIRE(triangles[0].vertices[0].color == std::array<u8, 4>{1, 2, 3, 4});
}

TEST_CASE("a multiplied matrix applies before the one it multiplies", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 translate = builder.addMatrix(makeTransform(One, {One, 0, 0}));
    const u32 scale = builder.addMatrix(makeTransform(2 * One, {0, 0, 0}));
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.applyMatrix(translate, Load);
    builder.applyMatrix(scale, 0);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[1].position[0] == Catch::Approx(21.0F));
}

TEST_CASE("popping restores the pushed modelview", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 translate = builder.addMatrix(makeTransform(One, {5 * One, 0, 0}));
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.applyMatrix(translate, Load | Push);
    builder.addWords(0xD838'0002, 64);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[1].position[0] == Catch::Approx(10.0F));
}

TEST_CASE("a segment set by the display list resolves later addresses", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.moveWord(0x06, 0x08 * 4, SegmentBase);
    builder.loadVertices((vertices & 0x00FF'FFFF) | 0x0800'0000, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.empty());
    REQUIRE(findTriangles(result).size() == 1);
}

TEST_CASE("a called display list returns, a branch does not", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 callee = builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.callDisplayList(callee);
    builder.branchDisplayList(callee);
    builder.drawTriangle(0, 1, 2);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.empty());
    REQUIRE(findTriangles(result).size() == 2);
}

TEST_CASE("culling follows the winding on screen", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.setGeometryMode(0, CullBack);
    builder.drawTriangle(0, 1, 2);
    builder.drawTriangle(0, 2, 1);
    builder.setGeometryMode(CullBack, CullFront);
    builder.drawTriangle(0, 1, 2);
    builder.drawTriangle(0, 2, 1);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 2);
    REQUIRE(triangles[0].vertices[1].position[0] == Catch::Approx(10.0F));
    REQUIRE(triangles[1].vertices[1].position[1] == Catch::Approx(10.0F));
}

TEST_CASE("flat shading takes the first vertex's colour", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.setGeometryMode(0, Shade);
    builder.drawTriangle(1, 2, 0);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].shade);
    for (const n64::ShadedVertex& vertex : triangles[0].vertices) {
        REQUIRE(vertex.color == std::array<u8, 4>{0, 255, 0, 255});
    }
}

TEST_CASE("lighting adds each light by its angle to the ambient colour", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 light = builder.addBytes({200, 0, 0, 0, 200, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0});
    const u32 ambient = builder.addBytes({10, 10, 10, 0, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const u32 vertices = builder.addVertex({0, 0, 0}, {0, 0}, {0, 0, 127, 255});
    builder.addVertex({10, 0, 0}, {0, 0}, {0, 0, 0x81, 255});
    builder.addVertex({0, 10, 0}, {0, 0}, {0, 90, 90, 255});
    const u32 start = builder.moveWord(0x02, 0, 24);
    builder.moveMemory(light, 16, 10, 0x30);
    builder.moveMemory(ambient, 16, 10, 0x48);
    builder.setGeometryMode(0, Lighting | Smooth);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    const std::vector<n64::Triangle> triangles = findTriangles(result);
    REQUIRE(result.diagnostics.empty());
    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[0].color == std::array<u8, 4>{210, 10, 10, 255});
    REQUIRE(triangles[0].vertices[1].color == std::array<u8, 4>{10, 10, 10, 255});
    REQUIRE(triangles[0].vertices[2].color[0] == 151);
}

TEST_CASE("fog replaces alpha by depth", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = builder.addVertex({0, 0, 30}, {0, 0}, {0, 0, 0, 255});
    builder.addVertex({0, 0, -5}, {0, 0}, {0, 0, 0, 255});
    builder.addVertex({0, 0, 200}, {0, 0}, {0, 0, 0, 255});
    const u32 start = builder.moveWord(0x08, 0, (2U << 16) | 10U);
    builder.setGeometryMode(0, Fog | Smooth);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[0].color[3] == 70);
    REQUIRE(triangles[0].vertices[1].color[3] == 10);
    REQUIRE(triangles[0].vertices[2].color[3] == 255);
}

TEST_CASE("texture coordinates are scaled by G_TEXTURE and given in texels", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = builder.addVertex({0, 0, 0}, {64, -32}, {0, 0, 0, 0});
    builder.addVertex({10, 0, 0}, {0, 0}, {0, 0, 0, 0});
    builder.addVertex({0, 10, 0}, {0, 0}, {0, 0, 0, 0});
    const u32 start = builder.setTexture(0x8000, 0x8000, 3, true);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].textured);
    REQUIRE(triangles[0].tile == 3);
    REQUIRE(triangles[0].vertices[0].texture[0] == Catch::Approx(1.0F));
    REQUIRE(triangles[0].vertices[0].texture[1] == Catch::Approx(-0.5F));
}

TEST_CASE("other mode halves reach the RDP as a whole G_RDPSETOTHERMODE", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.addWords(0xE300'1201, 0x0000'2000);
    builder.addWords(0xE200'001C, 0x0055'2078);
    builder.endDisplayList();

    const std::vector<n64::RdpCommand> commands = findRdpCommands(runDisplayList(builder, start));

    REQUIRE(commands.size() == 2);
    REQUIRE(commands[0].word0 == 0xEF00'2000);
    REQUIRE(commands[1].word0 == 0xEF00'2000);
    REQUIRE(commands[1].word1 == 0x0055'2078);
}

TEST_CASE("image addresses reach the RDP resolved", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.addWords(0xFD10'0000, 0x0600'0100);
    builder.endDisplayList();

    const std::vector<n64::RdpCommand> commands = findRdpCommands(runDisplayList(builder, start));

    REQUIRE(commands.size() == 1);
    REQUIRE(commands[0].word1 == SegmentBase + 0x100);
}

TEST_CASE("a texture rectangle takes its two halves along", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.addWords(0xE404'0040, 0x0000'0000);
    builder.addWords(0xE100'0000, 0x0000'0000);
    builder.addWords(0xF100'0000, 0x0400'0400);
    builder.addWords(0xE700'0000, 0);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.outputs.size() == 2);
    const auto& rectangle = std::get<n64::TextureRectangle>(result.outputs[0]);
    REQUIRE(rectangle.words[3] == 0x0400'0400);
    REQUIRE(std::get<n64::RdpCommand>(result.outputs[1]).word0 == 0xE700'0000);
}

TEST_CASE("G_BRANCH_Z branches when the vertex is nearer than the limit", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 near = builder.drawTriangle(0, 1, 2);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.addWords(0xE100'0000, near);
    builder.addWords(0x0400'5000, 600U << 16);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 2);
}

TEST_CASE("G_MODIFYVTX overwrites a loaded vertex's colour", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.addWords(0x0210'0002, 0x1122'3344);
    builder.setGeometryMode(0, Smooth);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 1);
    REQUIRE(triangles[0].vertices[1].color == std::array<u8, 4>{0x11, 0x22, 0x33, 0x44});
}

TEST_CASE("an unknown command is named with its address and skipped", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.addWords(0x4200'0000, 0);
    builder.addWords(0xE700'0000, 0);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].message.contains("0x100000"));
    REQUIRE(result.diagnostics[0].message.contains("0x42"));
    REQUIRE(findRdpCommands(result).size() == 1);
}

TEST_CASE("reading outside mapped memory is reported", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.loadVertices(0x0600'8000, 3, 0);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].message.contains("0x06008000"));
}

TEST_CASE("a vertex index past the buffer is reported", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.drawTriangle(0, 1, 40);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.outputs.empty());
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].message.contains("vertex 40"));
}

TEST_CASE("a display list that never ends is stopped", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.getAddress();
    builder.branchDisplayList(start);

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].severity == base::Severity::Error);
}

TEST_CASE("a triangle using a vertex no G_VTX loaded is reported", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 5);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.outputs.empty());
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].message.contains("vertex 5"));
}

TEST_CASE("an other mode field past the word is reported, not applied", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 start = builder.addWords(0xE300'2004, 0xFFFF'FFFF);
    builder.endDisplayList();

    const n64::RspResult result = runDisplayList(builder, start);

    REQUIRE(result.outputs.empty());
    REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("a light changed between vertex loads lights the later ones", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 ambient = builder.addBytes({10, 10, 10, 0, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const u32 vertices = builder.addVertex({0, 0, 0}, {0, 0}, {0, 0, 127, 255});
    builder.addVertex({10, 0, 0}, {0, 0}, {0, 0, 127, 255});
    builder.addVertex({0, 10, 0}, {0, 0}, {0, 0, 127, 255});
    const u32 start = builder.moveWord(0x02, 0, 0);
    builder.moveMemory(ambient, 16, 10, 0x30);
    builder.setGeometryMode(0, Lighting | Smooth);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.moveWord(0x0A, 0, 0x4040'4000);
    builder.loadVertices(vertices, 3, 0);
    builder.drawTriangle(0, 1, 2);
    builder.endDisplayList();

    const std::vector<n64::Triangle> triangles = findTriangles(runDisplayList(builder, start));

    REQUIRE(triangles.size() == 2);
    REQUIRE(triangles[0].vertices[0].color == std::array<u8, 4>{10, 10, 10, 255});
    REQUIRE(triangles[1].vertices[0].color == std::array<u8, 4>{0x40, 0x40, 0x40, 255});
}

TEST_CASE("a traced task records every command run, how deep, and what it made", "[n64][rsp]") {
    DisplayListBuilder builder;
    const u32 vertices = addTriangleVertices(builder);
    const u32 called = builder.setGeometryMode(0, CullBack);
    builder.drawTriangle(0, 1, 2);
    builder.drawTriangle(0, 2, 1);
    builder.endDisplayList();
    const u32 start = builder.loadVertices(vertices, 3, 0);
    builder.callDisplayList(called);
    builder.callDisplayList(called);
    builder.endDisplayList();

    n64::Memory memory;
    REQUIRE(memory.map(SegmentBase, builder.getBytes()));
    n64::RspTask task{.displayList = start, .recordsTrace = true};
    task.segments[DisplayListBuilder::Segment] = SegmentBase;
    const n64::RspResult result = n64::runTask(memory, task);

    REQUIRE(result.trace.size() == 12);
    REQUIRE(result.trace[0].address == SegmentBase + (start & 0x00FF'FFFF));
    REQUIRE(result.trace[0].depth == 0);
    REQUIRE(result.trace[2].depth == 1);
    REQUIRE(result.trace[3].endOutput - result.trace[3].firstOutput == 1);
    REQUIRE(result.trace[3].culled == 0);
    REQUIRE(result.trace[4].endOutput == result.trace[4].firstOutput);
    REQUIRE(result.trace[4].culled == 1);
    REQUIRE(result.trace[8].address == result.trace[3].address);
    REQUIRE(result.trace[8].firstOutput != result.trace[3].firstOutput);
    REQUIRE(n64::runTask(memory, {.displayList = start, .segments = task.segments}).trace.empty());
}
