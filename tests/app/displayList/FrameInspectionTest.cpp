#include "app/displayList/domain/FrameInspection.h"

#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "cparser/MacroTable.h"
#include "n64/DisplayListWriter.h"
#include "z64/GbiNames.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    REQUIRE(inspection.findFirstRun(frame.mappedImages.front().segments[Segment] +
                                    (start & 0x00FF'FFFF)) == callRow + 1);
    REQUIRE(inspection.findPhysicalName(frame.frameListAddress) == "Toki's frame list");

    const std::vector<toki::displayList::ScreenTriangle> drawn =
        inspection.listScreenTriangles(*triangle);
    REQUIRE(drawn.size() == 1);
    for (const glm::vec2& corner : drawn.front()) {
        REQUIRE(corner.x >= 0.0F);
        REQUIRE(corner.x <= 320.0F);
        REQUIRE(corner.y >= 0.0F);
        REQUIRE(corner.y <= 240.0F);
    }
    REQUIRE(inspection.listScreenTriangles(callRow).size() == 1);
    REQUIRE(inspection.listScreenTriangles(*vertex).empty());
    REQUIRE(inspection.findSymbolRun("gTestDL") == callRow + 1);
    REQUIRE_FALSE(inspection.findSymbolRun("gTestVtx"));
}
