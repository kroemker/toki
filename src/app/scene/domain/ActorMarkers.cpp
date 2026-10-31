#include "ActorMarkers.h"

#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/DisplayListWriter.h"
#include "n64/FixedMatrix.h"
#include "n64/Opcode.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize VertexBufferSize = 32;
constexpr std::array<u8, 4> EdgeColor{0x14, 0x15, 0x19, 0xFF};

/// Vertices are written in this many steps to a world unit, and every marker
/// is drawn under a matrix that scales them back, so an edge can be thinner
/// than the unit a world position is given in.
constexpr f32 LocalScale = 32.0F;

/// Half the width of an edge, in world units.
constexpr f32 EdgeHalfWidth = 0.6F;

/// `G_RM_ZB_OPA_SURF` in both cycles, as the reference grid draws.
constexpr u32 OpaqueDepthRenderMode = 0x0F0A'0230;

/// `G_CC_SHADE` in both cycles.
constexpr u32 ShadeCombine0 =
    0x00FF'FFFF & ((15U << 20) | (31U << 15) | (7U << 12) | (7U << 9) | (15U << 5) | 31U);
constexpr u32 ShadeCombine1 = (15U << 28) | (15U << 24) | (7U << 21) | (7U << 18) | (4U << 15) |
                              (7U << 12) | (4U << 9) | (4U << 6) | (7U << 3) | 4U;

constexpr f32 DiamondMiddle = 24.0F;
constexpr f32 DiamondRadius = 12.0F;
constexpr f32 CubeHalf = 15.0F;
constexpr f32 PinHalf = 12.0F;
constexpr f32 PinTop = 36.0F;
constexpr std::array<std::array<f32, 2>, 4> SquareRing{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

/// A box's triangles, its corners a ring at the bottom and then the same ring
/// at the top, each ring in `SquareRing`'s order, wound to face out.
constexpr std::array<std::array<u32, 3>, 12> BoxTriangles{{{0, 1, 2},
                                                           {0, 2, 3},
                                                           {4, 6, 5},
                                                           {4, 7, 6},
                                                           {0, 4, 5},
                                                           {0, 5, 1},
                                                           {1, 5, 6},
                                                           {1, 6, 2},
                                                           {2, 6, 7},
                                                           {2, 7, 3},
                                                           {3, 7, 4},
                                                           {3, 4, 0}}};
constexpr usize BoxCorners = 8;
constexpr usize EdgesPerLoad = VertexBufferSize / BoxCorners;

struct Corner {
    glm::vec3 offset{};
    bool isShaded = false;
};

/// A marker's corners, relative to where it is placed, its triangles as
/// indices into them, each wound so that it faces out, and its edges.
struct Shape {
    std::vector<Corner> corners;
    std::vector<std::array<u32, 3>> triangles;
    std::vector<std::array<u32, 2>> edges;
};

[[nodiscard]] Shape makeDiamond() {
    return Shape{
        .corners = {{.offset = {0, DiamondMiddle * 2, 0}},
                    {.offset = {0, 0, 0}, .isShaded = true},
                    {.offset = {DiamondRadius, DiamondMiddle, 0}},
                    {.offset = {0, DiamondMiddle, DiamondRadius}, .isShaded = true},
                    {.offset = {-DiamondRadius, DiamondMiddle, 0}},
                    {.offset = {0, DiamondMiddle, -DiamondRadius}, .isShaded = true}},
        .triangles = {{0, 3, 2},
                      {0, 4, 3},
                      {1, 2, 3},
                      {1, 3, 4},
                      {0, 5, 4},
                      {0, 2, 5},
                      {1, 4, 5},
                      {1, 5, 2}},
        .edges = {{0, 2},
                  {0, 3},
                  {0, 4},
                  {0, 5},
                  {1, 2},
                  {1, 3},
                  {1, 4},
                  {1, 5},
                  {2, 3},
                  {3, 4},
                  {4, 5},
                  {5, 2}},
    };
}

[[nodiscard]] Shape makeCube() {
    Shape cube;
    for (const f32 y : {0.0F, CubeHalf * 2}) {
        for (const auto& [x, z] : SquareRing) {
            cube.corners.push_back(
                Corner{.offset = {x * CubeHalf, y, z * CubeHalf}, .isShaded = y == 0.0F});
        }
    }
    cube.triangles.assign(BoxTriangles.begin(), BoxTriangles.end());
    const auto ringSize = static_cast<u32>(SquareRing.size());
    for (u32 a = 0; a < ringSize; ++a) {
        const u32 b = (a + 1) % ringSize;
        cube.edges.push_back({a, b});
        cube.edges.push_back({a + ringSize, b + ringSize});
        cube.edges.push_back({a, a + ringSize});
    }
    return cube;
}

/// A square pyramid standing on its tip, the tip where the marker is placed.
[[nodiscard]] Shape makePin() {
    Shape pin;
    for (usize i = 0; i < SquareRing.size(); ++i) {
        const auto& [x, z] = SquareRing[i];
        pin.corners.push_back(
            Corner{.offset = {x * PinHalf, PinTop, z * PinHalf}, .isShaded = i % 2 == 1});
    }
    const auto ringSize = static_cast<u32>(SquareRing.size());
    pin.corners.push_back(Corner{.offset = {0, 0, 0}, .isShaded = true});
    pin.triangles = {{0, 2, 1}, {0, 3, 2}};
    for (u32 a = 0; a < ringSize; ++a) {
        const u32 b = (a + 1) % ringSize;
        pin.triangles.push_back({ringSize, a, b});
        pin.edges.push_back({a, b});
        pin.edges.push_back({a, ringSize});
    }
    return pin;
}

[[nodiscard]] Shape makeShape(const core::MarkerShape kind) {
    switch (kind) {
    case core::MarkerShape::Cube:
        return makeCube();
    case core::MarkerShape::Diamond:
        return makeDiamond();
    case core::MarkerShape::Pin:
        return makePin();
    }
    return makeCube();
}

[[nodiscard]] std::array<u8, 4> findColor(const MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Actor:
        return {0xF2, 0x9B, 0x38, 0xFF};
    case MarkerKind::PlayerEntry:
        return {0x5C, 0xC9, 0x7A, 0xFF};
    case MarkerKind::TransitionActor:
        return {0x5A, 0xA3, 0xF2, 0xFF};
    case MarkerKind::PathPoint:
        return {0xD9, 0x6C, 0xE0, 0xFF};
    }
    return {0xFF, 0xFF, 0xFF, 0xFF};
}

[[nodiscard]] std::array<u8, 4> makeShaded(const std::array<u8, 4>& color) {
    return {static_cast<u8>(color[0] * 3 / 4), static_cast<u8>(color[1] * 3 / 4),
            static_cast<u8>(color[2] * 3 / 4), color[3]};
}

[[nodiscard]] u32 addLocalVertex(n64::DisplayListWriter& writer, const glm::vec3& offset,
                                 const std::array<u8, 4>& color) {
    const glm::vec3 local = offset * LocalScale;
    return writer.addVertex({.position = {static_cast<s16>(std::lround(local.x)),
                                          static_cast<s16>(std::lround(local.y)),
                                          static_cast<s16>(std::lround(local.z))},
                             .color = color});
}

/// The shape's corners in `color`, shaded where a corner says so.
[[nodiscard]] u32 addFill(n64::DisplayListWriter& writer, const Shape& shape,
                          const std::array<u8, 4>& color) {
    const std::array<u8, 4> shaded = makeShaded(color);
    const u32 address = writer.getAddress();
    for (const Corner& corner : shape.corners) {
        static_cast<void>(addLocalVertex(writer, corner.offset, corner.isShaded ? shaded : color));
    }
    return address;
}

[[nodiscard]] glm::vec3 findCenter(const Shape& shape) {
    glm::vec3 sum{};
    for (const Corner& corner : shape.corners) {
        sum += corner.offset;
    }
    return sum / static_cast<f32>(shape.corners.size());
}

/// A thin box along every edge, reaching past both of its corners by its own
/// half width so that the boxes of edges that meet close the corner.
[[nodiscard]] u32 addEdges(n64::DisplayListWriter& writer, const Shape& shape) {
    const glm::vec3 center = findCenter(shape);
    const u32 address = writer.getAddress();
    for (const auto& [from, to] : shape.edges) {
        const glm::vec3 start = shape.corners[from].offset;
        const glm::vec3 end = shape.corners[to].offset;
        const glm::vec3 along = glm::normalize(end - start);
        const glm::vec3 middle = ((start + end) * 0.5F) - center;
        const glm::vec3 out = glm::normalize(middle - (along * glm::dot(middle, along)));
        const glm::vec3 side = glm::cross(along, out);
        for (const glm::vec3& tip :
             {start - (along * EdgeHalfWidth), end + (along * EdgeHalfWidth)}) {
            for (const auto& [u, v] : SquareRing) {
                static_cast<void>(addLocalVertex(
                    writer, tip + (((side * u) + (out * v)) * EdgeHalfWidth), EdgeColor));
            }
        }
    }
    return address;
}

void drawTriangles(n64::DisplayListWriter& writer, const std::span<const std::array<u32, 3>> list,
                   const u32 base) {
    const auto at = [base](const std::array<u32, 3>& triangle) {
        return std::array<u32, 3>{base + triangle[0], base + triangle[1], base + triangle[2]};
    };
    for (usize i = 0; i < list.size(); i += 2) {
        if (i + 1 < list.size()) {
            writer.drawTriangles(at(list[i]), at(list[i + 1]));
        } else {
            writer.drawTriangle(at(list[i]));
        }
    }
}

struct ShapeData {
    Shape shape;
    std::array<u32, 4> fills{};
    u32 edges = 0;
};

void drawMarker(n64::DisplayListWriter& writer, const ShapeData& data, const MarkerKind kind) {
    const Shape& shape = data.shape;
    writer.loadVertices(data.fills[static_cast<usize>(kind)],
                        static_cast<u32>(shape.corners.size()), 0);
    drawTriangles(writer, shape.triangles, 0);
    for (usize first = 0; first < shape.edges.size(); first += EdgesPerLoad) {
        const usize count = std::min(EdgesPerLoad, shape.edges.size() - first);
        writer.loadVertices(data.edges + static_cast<u32>(first * BoxCorners * n64::VertexDataSize),
                            static_cast<u32>(count * BoxCorners), 0);
        for (usize edge = 0; edge < count; ++edge) {
            drawTriangles(writer, BoxTriangles, static_cast<u32>(edge * BoxCorners));
        }
    }
}

[[nodiscard]] glm::mat4 makeMarkerMatrix(const Marker& marker) {
    const glm::vec3 position{marker.position[0], marker.position[1], marker.position[2]};
    return glm::scale(glm::translate(glm::mat4(1.0F), position),
                      glm::vec3(marker.scale / LocalScale));
}

} // namespace

ActorMarkers makeActorMarkers(const std::span<const Marker> markers,
                              const core::MarkerShape shape) {
    n64::DisplayListWriter writer(MarkerSegment);
    ShapeData data{.shape = makeShape(shape)};
    for (const MarkerKind kind : {MarkerKind::Actor, MarkerKind::PlayerEntry,
                                  MarkerKind::TransitionActor, MarkerKind::PathPoint}) {
        data.fills[static_cast<usize>(kind)] = addFill(writer, data.shape, findColor(kind));
    }
    data.edges = addEdges(writer, data.shape);
    std::vector<u32> matrices;
    matrices.reserve(markers.size());
    for (const Marker& marker : markers) {
        matrices.push_back(writer.addMatrix(viewport::makeFixedMatrix(makeMarkerMatrix(marker))));
    }
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());

    constexpr u32 Unwanted = n64::geometry::Lighting | n64::geometry::Fog |
                             n64::geometry::TextureGen | n64::geometry::CullFront |
                             n64::geometry::CullBack;
    constexpr u32 Wanted = n64::geometry::ZBuffer | n64::geometry::Shade | n64::geometry::CullBack;
    ActorMarkers made;
    for (usize i = 0; i < markers.size(); ++i) {
        made.displayLists.push_back(writer.syncPipe());
        writer.setCombine(ShadeCombine0, ShadeCombine1);
        writer.setOtherMode(0, OpaqueDepthRenderMode);
        writer.setGeometryMode(Unwanted, Wanted);
        writer.loadMatrix(matrices[i], n64::MatrixLoad::ModelView);
        drawMarker(writer, data, markers[i].kind);
        writer.loadMatrix(identity, n64::MatrixLoad::ModelView);
        writer.endDisplayList();
    }
    made.image.segments.push_back(
        z64::ImageSegment{.number = MarkerSegment, .bytes = writer.getBytes()});
    return made;
}

} // namespace toki::scene
