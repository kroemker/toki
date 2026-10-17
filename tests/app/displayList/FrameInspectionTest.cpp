#include "app/displayList/domain/FrameInspection.h"

#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "cparser/MacroTable.h"
#include "n64/DisplayListWriter.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"
#include "z64/GbiNames.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using base::f32;
using base::s16;
using base::u32;
using base::u8;
using base::usize;

namespace {

constexpr u8 Segment = 0x06;

[[nodiscard]] std::optional<usize> findRow(const toki::displayList::FrameInspection& inspection,
                                           const std::string& name) {
    const auto& rows = inspection.getRows();
    const auto found = std::ranges::find_if(
        rows, [&name](const auto& row) { return row.command.name == name && row.depth > 0; });
    return found == rows.end() ? std::nullopt
                               : std::optional<usize>(static_cast<usize>(found - rows.begin()));
}

[[nodiscard]] toki::viewport::ViewportContent makeContent(const n64::DisplayListWriter& writer,
                                                          const u32 start) {
    z64::MemoryImage image;
    image.segments.push_back(z64::ImageSegment{.number = Segment, .bytes = writer.getBytes()});
    toki::viewport::ViewportContent content;
    content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    content.draws.push_back(toki::viewport::ViewportDraw{.image = 0, .displayList = start});
    return content;
}

[[nodiscard]] s16 makeCoordinate(const f32 value) {
    return static_cast<s16>(std::lround(value));
}

} // namespace

TEST_CASE("a traced frame reads as its commands, named by the content's symbols", "[displayList]") {
    n64::DisplayListWriter writer(Segment);
    const u32 vertices = writer.addVertex({.position = {0, 0, 0}, .color = {255, 255, 255, 255}});
    static_cast<void>(writer.addVertex({.position = {50, 0, 0}, .color = {255, 255, 255, 255}}));
    static_cast<void>(writer.addVertex({.position = {0, 50, 0}, .color = {255, 255, 255, 255}}));
    const u32 start = writer.loadVertices(vertices, 3, 0);
    writer.drawTriangle({0, 1, 2});
    writer.endDisplayList();

    z64::MemoryImage image;
    image.segments.push_back(z64::ImageSegment{.number = Segment, .bytes = writer.getBytes()});
    image.symbols["gTestVtx"] =
        z64::ImageSymbol{.segment = Segment, .offset = vertices & 0x00FF'FFFF, .size = 48};
    image.symbols["gTestDL"] =
        z64::ImageSymbol{.segment = Segment, .offset = start & 0x00FF'FFFF, .size = 24};
    toki::viewport::ViewportContent content;
    content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    content.draws.push_back(toki::viewport::ViewportDraw{.image = 0, .displayList = start});
    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, toki::viewport::OrbitCamera{}, {}, true);
    REQUIRE_FALSE(frame.rspResult.trace.empty());

    const toki::displayList::FrameInspection inspection(frame, content);
    const cparser::MacroTable gbi;
    const z64::GbiNames names(gbi);

    const std::optional<usize> vertex = findRow(inspection, "gsSPVertex");
    REQUIRE(vertex.has_value());
    REQUIRE(inspection.describeEffect(*vertex, names) ==
            "Loaded 3 vertices into slots 0 to 2 from gTestVtx.");
    const std::optional<usize> triangle = findRow(inspection, "gsSP1Triangle");
    REQUIRE(triangle.has_value());
    REQUIRE(inspection.listDraws(*triangle).size() == 1);
    REQUIRE(inspection.findDrawState(*triangle) != nullptr);
    REQUIRE(inspection.describeEffect(*triangle, names) == "Drew 1 triangle.");

    const auto& rows = inspection.getRows();
    const auto call = std::ranges::find_if(rows, [&](const auto& row) {
        return row.command.name == "gsSPDisplayList" &&
               inspection.findAddressName(static_cast<usize>(&row - rows.data()),
                                          row.command.fields.front().value) == "gTestDL";
    });
    REQUIRE(call != rows.end());
    const usize callRow = static_cast<usize>(call - rows.begin());
    REQUIRE(call->end == callRow + 4);
    REQUIRE(inspection.describeEffect(callRow, names) ==
            "Called gTestDL, which ran 3 commands before returning here.");
    REQUIRE(inspection.findFirstRun(frame.mappedImages.front()->segments[Segment] +
                                    (start & 0x00FF'FFFF)) == callRow + 1);
    REQUIRE(inspection.findPhysicalName(frame.frameListAddress) == "Toki's frame list");

    const n64::Viewport& viewport = frame.rspResult.viewports.front();
    const std::vector<toki::viewport::ClipTriangle> drawn =
        inspection.listClipTriangles(*triangle, viewport);
    REQUIRE(drawn.size() == 1);
    for (usize i = 0; i < 3; ++i) {
        const toki::viewport::ClipPosition& corner = drawn.front()[i];
        const std::array<f32, 4>& drawnCorner =
            std::get<n64::Triangle>(
                frame.rdpResult.draws[inspection.listDraws(*triangle).front()].primitive)
                .vertices[i]
                .position;
        for (usize axis = 0; axis < 4; ++axis) {
            REQUIRE(corner[axis] == Catch::Approx(drawnCorner[axis]).margin(1.0e-4));
        }
    }
    REQUIRE(inspection.listClipTriangles(callRow, viewport).size() == 1);
    REQUIRE(inspection.listClipTriangles(*vertex, viewport).empty());
    REQUIRE(inspection.findSymbolRun("gTestDL") == callRow + 1);
    REQUIRE_FALSE(inspection.findSymbolRun("gTestVtx"));
}

TEST_CASE("a triangle is placed where it showed, in the clip space of another viewport",
          "[displayList]") {
    n64::DisplayListWriter writer(Segment);
    const u32 vertices = writer.addVertex({.position = {0, 0, 0}, .color = {255, 255, 255, 255}});
    static_cast<void>(writer.addVertex({.position = {300, 0, 0}, .color = {255, 255, 255, 255}}));
    static_cast<void>(writer.addVertex({.position = {0, 300, 0}, .color = {255, 255, 255, 255}}));
    const u32 start = writer.loadVertices(vertices, 3, 0);
    writer.drawTriangles({0, 1, 2}, {0, 2, 1});
    writer.endDisplayList();
    const toki::viewport::ViewportContent content = makeContent(writer, start);
    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, toki::viewport::OrbitCamera{}, {}, true);
    const toki::displayList::FrameInspection inspection(frame, content);
    const std::optional<usize> row = findRow(inspection, "gsSP2Triangles");
    REQUIRE(row.has_value());
    REQUIRE_FALSE(inspection.listDraws(*row).empty());

    const n64::Viewport target{.scale = {400.0F, 250.0F, 511.0F},
                               .translate = {420.0F, 260.0F, 511.0F}};
    const std::vector<toki::viewport::ClipTriangle> placed =
        inspection.listClipTriangles(*row, target);
    REQUIRE(placed.size() == inspection.listDraws(*row).size());
    const auto& drawn = std::get<n64::Triangle>(
        frame.rdpResult.draws[inspection.listDraws(*row).front()].primitive);
    const n64::Viewport& viewport = frame.rspResult.viewports.at(drawn.viewport);
    for (usize i = 0; i < 3; ++i) {
        const toki::viewport::ClipPosition& corner = placed.front()[i];
        const auto& [x, y, z, w] = drawn.vertices[i].position;
        REQUIRE((corner[0] / corner[3] * target.scale[0]) + target.translate[0] ==
                Catch::Approx((x / w * viewport.scale[0]) + viewport.translate[0]).margin(1.0e-2));
        REQUIRE((-corner[1] / corner[3] * target.scale[1]) + target.translate[1] ==
                Catch::Approx((-y / w * viewport.scale[1]) + viewport.translate[1]).margin(1.0e-2));
        REQUIRE(corner[2] == Catch::Approx(z));
        REQUIRE(corner[3] == Catch::Approx(w));
    }
}

TEST_CASE("a triangle with a corner behind the eye is kept whole", "[displayList]") {
    const toki::viewport::OrbitCamera camera;
    const glm::vec3 behind = camera.getEye() * 1.5F;
    n64::DisplayListWriter writer(Segment);
    const u32 vertices = writer.addVertex(
        {.position = {makeCoordinate(behind.x), makeCoordinate(behind.y), makeCoordinate(behind.z)},
         .color = {255, 255, 255, 255}});
    static_cast<void>(
        writer.addVertex({.position = {-300, 0, 300}, .color = {255, 255, 255, 255}}));
    static_cast<void>(
        writer.addVertex({.position = {300, 0, -300}, .color = {255, 255, 255, 255}}));
    const u32 start = writer.loadVertices(vertices, 3, 0);
    writer.drawTriangles({0, 1, 2}, {0, 2, 1});
    writer.endDisplayList();
    const toki::viewport::ViewportContent content = makeContent(writer, start);
    const toki::viewport::Frame frame = toki::viewport::runFrame(content, camera, {}, true);
    const toki::displayList::FrameInspection inspection(frame, content);
    const std::optional<usize> row = findRow(inspection, "gsSP2Triangles");
    REQUIRE(row.has_value());
    const std::vector<usize> draws = inspection.listDraws(*row);
    REQUIRE_FALSE(draws.empty());
    const auto& drawn = std::get<n64::Triangle>(frame.rdpResult.draws[draws.front()].primitive);
    REQUIRE(drawn.vertices[0].position[3] < 0.0F);

    const std::vector<toki::viewport::ClipTriangle> placed =
        inspection.listClipTriangles(*row, frame.rspResult.viewports.at(drawn.viewport));
    REQUIRE(placed.size() == draws.size());
    for (usize i = 0; i < 3; ++i) {
        for (usize axis = 0; axis < 4; ++axis) {
            REQUIRE(placed.front()[i][axis] ==
                    Catch::Approx(drawn.vertices[i].position[axis]).margin(1.0e-3));
        }
    }
}

TEST_CASE("a rectangle is placed at the near plane, over the pixels it covered", "[displayList]") {
    n64::DisplayListWriter writer(Segment);
    const u32 start =
        writer.addCommand(0xF6000000U | (40U << 14) | (30U << 2), (8U << 14) | (4U << 2));
    writer.endDisplayList();
    const toki::viewport::ViewportContent content = makeContent(writer, start);
    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, toki::viewport::OrbitCamera{}, {}, true);
    const toki::displayList::FrameInspection inspection(frame, content);
    const std::optional<usize> row = findRow(inspection, "gsDPFillRectangle");
    REQUIRE(row.has_value());

    const n64::Viewport target{.scale = {100.0F, 50.0F, 511.0F},
                               .translate = {100.0F, 50.0F, 511.0F}};
    const std::vector<toki::viewport::ClipTriangle> placed =
        inspection.listClipTriangles(*row, target);
    REQUIRE(placed.size() == 2);
    const toki::viewport::ClipPosition& topLeft = placed.front()[0];
    const toki::viewport::ClipPosition& bottomRight = placed.front()[2];
    REQUIRE(topLeft[0] == Catch::Approx(-0.92F));
    REQUIRE(topLeft[1] == Catch::Approx(0.92F));
    REQUIRE(topLeft[2] == Catch::Approx(-0.99F));
    REQUIRE(topLeft[3] == 1.0F);
    REQUIRE(bottomRight[0] == Catch::Approx(-0.59F));
    REQUIRE(bottomRight[1] == Catch::Approx(0.38F));
}
