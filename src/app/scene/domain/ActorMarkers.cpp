#include "ActorMarkers.h"

#include "app/scene/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/Opcode.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize VerticesPerMarker = 6;
constexpr usize MarkersPerLoad = 5;
constexpr std::array<u8, 4> OutlineColor{0x14, 0x15, 0x19, 0xFF};

/// `G_RM_ZB_OPA_SURF` in both cycles, as the reference grid draws.
constexpr u32 OpaqueDepthRenderMode = 0x0F0A'0230;

/// `G_CC_SHADE` in both cycles.
constexpr u32 ShadeCombine0 =
    0x00FF'FFFF & ((15U << 20) | (31U << 15) | (7U << 12) | (7U << 9) | (15U << 5) | 31U);
constexpr u32 ShadeCombine1 = (15U << 28) | (15U << 24) | (7U << 21) | (7U << 18) | (4U << 15) |
                              (7U << 12) | (4U << 9) | (4U << 6) | (7U << 3) | 4U;

struct DiamondSize {
    s16 radius = 0;
    s16 halfHeight = 0;
};

constexpr DiamondSize MarkerSize{.radius = 12, .halfHeight = 24};
constexpr DiamondSize OutlineSize{.radius = 16, .halfHeight = 29};

[[nodiscard]] std::array<u8, 4> findColor(const MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Actor:
        return {0xF2, 0x9B, 0x38, 0xFF};
    case MarkerKind::PlayerEntry:
        return {0x5C, 0xC9, 0x7A, 0xFF};
    case MarkerKind::TransitionActor:
        return {0x5A, 0xA3, 0xF2, 0xFF};
    }
    return {0xFF, 0xFF, 0xFF, 0xFF};
}

[[nodiscard]] std::array<u8, 4> makeShaded(const std::array<u8, 4>& color) {
    return {static_cast<u8>(color[0] * 3 / 4), static_cast<u8>(color[1] * 3 / 4),
            static_cast<u8>(color[2] * 3 / 4), color[3]};
}

[[nodiscard]] u32 addDiamond(n64::DisplayListWriter& writer, const std::array<s16, 3>& position,
                             const DiamondSize size, const std::array<u8, 4>& color) {
    const std::array<u8, 4> shaded = makeShaded(color);
    const auto at = [&position](const int dx, const int dy, const int dz) {
        return std::array<s16, 3>{static_cast<s16>(position[0] + dx),
                                  static_cast<s16>(position[1] + dy),
                                  static_cast<s16>(position[2] + dz)};
    };
    const int middle = MarkerSize.halfHeight;
    const int radius = size.radius;
    const int half = size.halfHeight;
    const u32 address = writer.addVertex({.position = at(0, middle + half, 0), .color = color});
    static_cast<void>(writer.addVertex({.position = at(0, middle - half, 0), .color = shaded}));
    static_cast<void>(writer.addVertex({.position = at(radius, middle, 0), .color = color}));
    static_cast<void>(writer.addVertex({.position = at(0, middle, radius), .color = shaded}));
    static_cast<void>(writer.addVertex({.position = at(-radius, middle, 0), .color = color}));
    static_cast<void>(writer.addVertex({.position = at(0, middle, -radius), .color = shaded}));
    return address;
}

void drawDiamond(n64::DisplayListWriter& writer, const u32 base) {
    const u32 top = base;
    const u32 bottom = base + 1;
    const std::array<u32, 4> ring{base + 2, base + 3, base + 4, base + 5};
    for (usize i = 0; i < ring.size(); i += 2) {
        const u32 a = ring[i];
        const u32 b = ring[(i + 1) % ring.size()];
        const u32 c = ring[(i + 2) % ring.size()];
        writer.drawTriangles({top, b, a}, {top, c, b});
        writer.drawTriangles({bottom, a, b}, {bottom, b, c});
    }
}

[[nodiscard]] std::vector<u32> addDiamonds(n64::DisplayListWriter& writer,
                                           const std::span<const Marker> markers,
                                           const DiamondSize size, const bool isOutline) {
    std::vector<u32> batches;
    for (usize i = 0; i < markers.size(); ++i) {
        const std::array<u8, 4> color = isOutline ? OutlineColor : findColor(markers[i].kind);
        const u32 address = addDiamond(writer, markers[i].position, size, color);
        if (i % MarkersPerLoad == 0) {
            batches.push_back(address);
        }
    }
    return batches;
}

void drawDiamonds(n64::DisplayListWriter& writer, const std::vector<u32>& batches,
                  const usize markerCount) {
    for (usize batch = 0; batch < batches.size(); ++batch) {
        const usize count = std::min(MarkersPerLoad, markerCount - (batch * MarkersPerLoad));
        writer.loadVertices(batches[batch], static_cast<u32>(count * VerticesPerMarker), 0);
        for (usize marker = 0; marker < count; ++marker) {
            drawDiamond(writer, static_cast<u32>(marker * VerticesPerMarker));
        }
    }
}

} // namespace

ActorMarkers makeActorMarkers(const std::span<const Marker> markers) {
    n64::DisplayListWriter writer(MarkerSegment);
    const std::vector<u32> outlines = addDiamonds(writer, markers, OutlineSize, true);
    const std::vector<u32> diamonds = addDiamonds(writer, markers, MarkerSize, false);

    constexpr u32 Unwanted = n64::geometry::Lighting | n64::geometry::Fog |
                             n64::geometry::TextureGen | n64::geometry::CullFront |
                             n64::geometry::CullBack;
    constexpr u32 Wanted = n64::geometry::ZBuffer | n64::geometry::Shade;
    const u32 start = writer.syncPipe();
    writer.setCombine(ShadeCombine0, ShadeCombine1);
    writer.setOtherMode(0, OpaqueDepthRenderMode);
    writer.setGeometryMode(Unwanted, Wanted | n64::geometry::CullFront);
    drawDiamonds(writer, outlines, markers.size());
    writer.setGeometryMode(Unwanted, Wanted | n64::geometry::CullBack);
    drawDiamonds(writer, diamonds, markers.size());
    writer.endDisplayList();

    ActorMarkers made;
    made.image.segments.push_back(
        z64::ImageSegment{.number = MarkerSegment, .bytes = writer.getBytes()});
    made.displayList = start;
    return made;
}

} // namespace toki::scene
