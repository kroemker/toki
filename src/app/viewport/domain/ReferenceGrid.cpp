#include "ReferenceGrid.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/DisplayListWriter.h"
#include "n64/Opcode.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr s16 CellSize = 100;
constexpr s16 CellsPerSide = 16;
constexpr usize CellsPerLoad = 8;
constexpr std::array<u8, 4> LightColor{0x5A, 0x5C, 0x66, 0xFF};
constexpr std::array<u8, 4> DarkColor{0x44, 0x46, 0x4E, 0xFF};

/// `G_RM_ZB_OPA_SURF` in both cycles: `Z_CMP | Z_UPD | CVG_DST_FULL`, and a
/// blender that passes the pixel through.
constexpr u32 OpaqueDepthRenderMode = 0x0F0A'0230;

/// `G_CC_SHADE` in both cycles.
constexpr u32 ShadeCombine0 =
    0x00FF'FFFF & ((15U << 20) | (31U << 15) | (7U << 12) | (7U << 9) | (15U << 5) | 31U);
constexpr u32 ShadeCombine1 = (15U << 28) | (15U << 24) | (7U << 21) | (7U << 18) | (4U << 15) |
                              (7U << 12) | (4U << 9) | (4U << 6) | (7U << 3) | 4U;

struct Cell {
    s16 x = 0;
    s16 z = 0;
    std::array<u8, 4> color{};
};

std::vector<Cell> makeCells() {
    std::vector<Cell> cells;
    constexpr s16 Half = CellsPerSide / 2;
    for (s16 row = -Half; row < Half; ++row) {
        for (s16 column = -Half; column < Half; ++column) {
            const bool light = ((row + column) & 1) == 0;
            cells.push_back(Cell{.x = static_cast<s16>(column * CellSize),
                                 .z = static_cast<s16>(row * CellSize),
                                 .color = light ? LightColor : DarkColor});
        }
    }
    return cells;
}

u32 addCellVertices(n64::DisplayListWriter& writer, const Cell& cell) {
    const auto right = static_cast<s16>(cell.x + CellSize);
    const auto back = static_cast<s16>(cell.z + CellSize);
    const u32 address = writer.addVertex({.position = {cell.x, 0, cell.z}, .color = cell.color});
    static_cast<void>(writer.addVertex({.position = {right, 0, cell.z}, .color = cell.color}));
    static_cast<void>(writer.addVertex({.position = {cell.x, 0, back}, .color = cell.color}));
    static_cast<void>(writer.addVertex({.position = {right, 0, back}, .color = cell.color}));
    return address;
}

} // namespace

ViewportContent makeReferenceGrid() {
    const std::vector<Cell> cells = makeCells();
    n64::DisplayListWriter writer(ReferenceGridSegment);
    std::vector<u32> batches;
    for (usize first = 0; first < cells.size(); first += CellsPerLoad) {
        batches.push_back(addCellVertices(writer, cells[first]));
        for (usize i = first + 1; i < first + CellsPerLoad && i < cells.size(); ++i) {
            static_cast<void>(addCellVertices(writer, cells[i]));
        }
    }

    const u32 start = writer.syncPipe();
    writer.setCombine(ShadeCombine0, ShadeCombine1);
    writer.setOtherMode(0, OpaqueDepthRenderMode);
    writer.setGeometryMode(n64::geometry::CullFront | n64::geometry::CullBack, 0);
    for (usize batch = 0; batch < batches.size(); ++batch) {
        const usize count = std::min(CellsPerLoad, cells.size() - (batch * CellsPerLoad));
        writer.loadVertices(batches[batch], static_cast<u32>(count * 4), 0);
        for (u32 cell = 0; cell < count; ++cell) {
            const u32 base = cell * 4;
            writer.drawTriangles({base, base + 1, base + 2}, {base + 1, base + 3, base + 2});
        }
    }
    writer.endDisplayList();

    const usize size = writer.getBytes().size();
    z64::MemoryImage image;
    image.segments.push_back(
        z64::ImageSegment{.number = ReferenceGridSegment, .bytes = writer.getBytes()});
    image.symbols.emplace("referenceGridDL",
                          z64::ImageSymbol{.segment = ReferenceGridSegment,
                                           .offset = start & 0x00FF'FFFFU,
                                           .size = size - (start & 0x00FF'FFFFU)});
    ViewportContent grid;
    grid.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    grid.draws.push_back(ViewportDraw{.image = 0, .displayList = start});
    return grid;
}

} // namespace toki::viewport
