#include "CollisionDrawing.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "n64/DisplayListWriter.h"
#include "n64/Opcode.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr std::array<std::string_view, CollisionColoringCount> ColoringLabels{
    "Kind",      "Surface Type", "Material",     "Floor Type",
    "Wall Type", "Exit",         "Hookshotable", "Conveyor"};

/// `G_RM_ZB_OPA_SURF` in both cycles.
constexpr u32 OpaqueRenderMode = 0x0F0A'0230;

/// `G_RM_ZB_XLU_DECAL` in both cycles: blended over what is drawn, compared as
/// a decal and not written to the depth buffer.
constexpr u32 TranslucentRenderMode = 0x0050'4E50;

/// `G_RM_ZB_XLU_SURF` in both cycles.
constexpr u32 WaterRenderMode = 0x0050'4A50;

/// `G_CC_SHADE` in both cycles.
constexpr u32 ShadeCombine0 =
    0x00FF'FFFF & ((15U << 20) | (31U << 15) | (7U << 12) | (7U << 9) | (15U << 5) | 31U);
constexpr u32 ShadeCombine1 = (15U << 28) | (15U << 24) | (7U << 21) | (7U << 18) | (4U << 15) |
                              (7U << 12) | (4U << 9) | (4U << 6) | (7U << 3) | 4U;

constexpr u8 OpaqueAlpha = 0xFF;
constexpr u8 TranslucentAlpha = 0x90;
constexpr std::array<u8, 3> FloorColor{0x4F, 0xA3, 0xE0};
constexpr std::array<u8, 3> WallColor{0xE0, 0xA0, 0x40};
constexpr std::array<u8, 3> CeilingColor{0xB0, 0x70, 0xD0};
constexpr std::array<u8, 3> PlainColor{0x80, 0x84, 0x8C};
constexpr std::array<u8, 3> TintColor{0xFF, 0xF0, 0x80};
constexpr f32 GoldenRatio = 0.618034F;
constexpr f32 Saturation = 0.6F;
constexpr f32 Value = 0.92F;
constexpr f32 AmbientShade = 0.55F;
constexpr glm::vec3 LightDirection{0.35F, 0.85F, 0.4F};
constexpr std::array<u8, 4> WaterSurfaceColor{0x50, 0x9C, 0xFF, 0x78};
constexpr std::array<u8, 4> WaterDepthColor{0x18, 0x3C, 0xA0, 0x78};
constexpr u32 BoxCorners = 4;
constexpr u32 UnwantedGeometry = n64::geometry::Lighting | n64::geometry::Fog |
                                 n64::geometry::TextureGen | n64::geometry::CullFront |
                                 n64::geometry::CullBack;
constexpr u32 WantedGeometry = n64::geometry::ZBuffer | n64::geometry::Shade;

[[nodiscard]] std::array<u8, 3> makeHueColor(const f32 hue) {
    const f32 sector = std::fmod(hue, 1.0F) * 6.0F;
    const f32 fraction = sector - std::floor(sector);
    const f32 low = Value * (1.0F - Saturation);
    const f32 falling = Value * (1.0F - (Saturation * fraction));
    const f32 rising = Value * (1.0F - (Saturation * (1.0F - fraction)));
    const std::array<std::array<f32, 3>, 6> sectors{{{Value, rising, low},
                                                     {falling, Value, low},
                                                     {low, Value, rising},
                                                     {low, falling, Value},
                                                     {rising, low, Value},
                                                     {Value, low, falling}}};
    const std::array<f32, 3>& rgb = sectors[static_cast<usize>(sector) % sectors.size()];
    return {static_cast<u8>(rgb[0] * 255.0F), static_cast<u8>(rgb[1] * 255.0F),
            static_cast<u8>(rgb[2] * 255.0F)};
}

/// A colour of its own for each value, the ones next to each other far apart.
[[nodiscard]] std::array<u8, 3> makeValueColor(const usize value) {
    return makeHueColor(static_cast<f32>(value) * GoldenRatio);
}

/// `value`'s own colour where it is not zero, which means the setting is off.
[[nodiscard]] std::array<u8, 3> makeSettingColor(const usize value) {
    return value == 0 ? PlainColor : makeValueColor(value);
}

[[nodiscard]] std::array<u8, 3> findKindColor(const CollisionPolygon& polygon) {
    switch (findPolygonKind(polygon)) {
    case PolygonKind::Floor:
        return FloorColor;
    case PolygonKind::Wall:
        return WallColor;
    case PolygonKind::Ceiling:
        return CeilingColor;
    }
    return PlainColor;
}

[[nodiscard]] std::array<u8, 3> findSettingColor(const SurfaceFields& fields,
                                                 const CollisionColoring coloring) {
    switch (coloring) {
    case CollisionColoring::Material:
        return makeValueColor(fields.material);
    case CollisionColoring::FloorType:
        return makeSettingColor(fields.floorType);
    case CollisionColoring::WallType:
        return makeSettingColor(fields.wallType);
    case CollisionColoring::Exit:
        return makeSettingColor(fields.exitIndex);
    case CollisionColoring::Hookshot:
        return makeSettingColor(fields.canHookshot ? 1 : 0);
    case CollisionColoring::Conveyor:
        return makeSettingColor(fields.conveyorSpeed);
    case CollisionColoring::Kind:
    case CollisionColoring::SurfaceType:
        break;
    }
    return PlainColor;
}

[[nodiscard]] std::array<u8, 3> mixColors(const std::array<u8, 3>& from,
                                          const std::array<u8, 3>& to) {
    return {static_cast<u8>((from[0] + to[0]) / 2), static_cast<u8>((from[1] + to[1]) / 2),
            static_cast<u8>((from[2] + to[2]) / 2)};
}

[[nodiscard]] std::array<u8, 4> shadeColor(const std::array<u8, 3>& color,
                                           const CollisionPolygon& polygon, const u8 alpha) {
    const glm::vec3 normal{polygon.normal[0], polygon.normal[1], polygon.normal[2]};
    const f32 length = glm::length(normal);
    const f32 facing =
        length == 0.0F ? 1.0F : std::abs(glm::dot(normal / length, glm::normalize(LightDirection)));
    const f32 shade = AmbientShade + ((1.0F - AmbientShade) * facing);
    return {static_cast<u8>(static_cast<f32>(color[0]) * shade),
            static_cast<u8>(static_cast<f32>(color[1]) * shade),
            static_cast<u8>(static_cast<f32>(color[2]) * shade), alpha};
}

[[nodiscard]] bool hasVertices(const Collision& collision, const CollisionPolygon& polygon) {
    for (usize corner = 0; corner < 3; ++corner) {
        if (getVertexIndex(polygon, corner) >= collision.vertices.size()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] s16 clampToS16(const s32 value) {
    return static_cast<s16>(std::clamp<s32>(value, INT16_MIN, INT16_MAX));
}

/// The box's corners on its surface, then the same below it at `bottom`.
[[nodiscard]] u32 addWaterBoxVertices(n64::DisplayListWriter& writer, const WaterBox& box,
                                      const s16 bottom) {
    const s16 xMax = clampToS16(s32{box.xMin} + box.xLength);
    const s16 zMax = clampToS16(s32{box.zMin} + box.zLength);
    const std::array<std::array<s16, 2>, BoxCorners> corners{
        {{box.xMin, box.zMin}, {xMax, box.zMin}, {xMax, zMax}, {box.xMin, zMax}}};
    const u32 address = writer.getAddress();
    for (const auto& [y, color] :
         {std::pair{box.ySurface, WaterSurfaceColor}, std::pair{bottom, WaterDepthColor}}) {
        for (const std::array<s16, 2>& corner : corners) {
            static_cast<void>(
                writer.addVertex({.position = {corner[0], y, corner[1]}, .color = color}));
        }
    }
    return address;
}

void drawWaterBox(n64::DisplayListWriter& writer, const u32 vertices, const bool hasDepth) {
    writer.loadVertices(vertices, hasDepth ? BoxCorners * 2 : BoxCorners, 0);
    writer.drawTriangles({0, 1, 2}, {0, 2, 3});
    if (!hasDepth) {
        return;
    }
    writer.drawTriangles({4, 6, 5}, {4, 7, 6});
    for (u32 corner = 0; corner < BoxCorners; ++corner) {
        const u32 next = (corner + 1) % BoxCorners;
        writer.drawTriangles({corner, next, next + BoxCorners},
                             {corner, next + BoxCorners, corner + BoxCorners});
    }
}

void addWaterBoxes(n64::DisplayListWriter& writer, const Collision& collision,
                   CollisionDrawing& drawing) {
    std::vector<u32> vertices;
    for (const WaterBox& box : collision.waterBoxes) {
        vertices.push_back(
            addWaterBoxVertices(writer, box, std::min(collision.minBounds[1], box.ySurface)));
    }
    drawing.waterBoxSetup = writer.syncPipe();
    writer.setCombine(ShadeCombine0, ShadeCombine1);
    writer.setOtherMode(0, WaterRenderMode);
    writer.setGeometryMode(UnwantedGeometry, WantedGeometry);
    writer.endDisplayList();
    for (usize i = 0; i < collision.waterBoxes.size(); ++i) {
        const WaterBox& box = collision.waterBoxes[i];
        drawing.waterBoxes.push_back(writer.getAddress());
        drawWaterBox(writer, vertices[i], collision.minBounds[1] < box.ySurface);
        writer.endDisplayList();
    }
}

} // namespace

std::string_view getCollisionColoringLabel(const CollisionColoring coloring) {
    return ColoringLabels[static_cast<usize>(coloring)];
}

std::string makeCollisionColoringKey(const CollisionColoring coloring) {
    std::string key;
    for (const char letter : getCollisionColoringLabel(coloring)) {
        key += letter == ' ' ? '_'
                             : static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    }
    return key;
}

std::optional<CollisionColoring> findCollisionColoring(const std::string_view key) {
    for (usize i = 0; i < CollisionColoringCount; ++i) {
        const auto coloring = static_cast<CollisionColoring>(i);
        if (makeCollisionColoringKey(coloring) == key) {
            return coloring;
        }
    }
    return std::nullopt;
}

std::array<u8, 3> findSurfaceTypeColor(const usize type) {
    return makeValueColor(type);
}

std::array<u8, 3> findPolygonColor(const Collision& collision, const CollisionPolygon& polygon,
                                   const CollisionLook& look) {
    std::array<u8, 3> color = PlainColor;
    if (look.coloring == CollisionColoring::Kind) {
        color = findKindColor(polygon);
    } else if (look.coloring == CollisionColoring::SurfaceType) {
        color = findSurfaceTypeColor(polygon.type);
    } else if (polygon.type < collision.surfaceTypes.size()) {
        color = findSettingColor(readSurfaceFields(collision.surfaceTypes[polygon.type]),
                                 look.coloring);
    }
    if (look.tintedSurfaceType == polygon.type) {
        color = mixColors(color, TintColor);
    }
    return color;
}

CollisionDrawing makeCollisionDrawing(const Collision& collision, const CollisionLook& look) {
    n64::DisplayListWriter writer(CollisionSegment);
    const u8 alpha = look.isTranslucent ? TranslucentAlpha : OpaqueAlpha;
    std::vector<u32> vertices(collision.polygons.size());
    for (usize i = 0; i < collision.polygons.size(); ++i) {
        const CollisionPolygon& polygon = collision.polygons[i];
        if (!hasVertices(collision, polygon)) {
            continue;
        }
        const std::array<u8, 4> color =
            shadeColor(findPolygonColor(collision, polygon, look), polygon, alpha);
        vertices[i] = writer.getAddress();
        for (usize corner = 0; corner < 3; ++corner) {
            static_cast<void>(writer.addVertex(
                {.position = collision.vertices[getVertexIndex(polygon, corner)], .color = color}));
        }
    }

    CollisionDrawing drawing{.setup = writer.syncPipe()};
    writer.setCombine(ShadeCombine0, ShadeCombine1);
    writer.setOtherMode(0, look.isTranslucent ? TranslucentRenderMode : OpaqueRenderMode);
    writer.setGeometryMode(UnwantedGeometry, WantedGeometry);
    writer.endDisplayList();
    for (const u32 address : vertices) {
        if (address == 0) {
            drawing.polygons.push_back(0);
            continue;
        }
        drawing.polygons.push_back(writer.loadVertices(address, 3, 0));
        writer.drawTriangle({0, 1, 2});
        writer.endDisplayList();
    }
    addWaterBoxes(writer, collision, drawing);
    drawing.image.segments.push_back(
        z64::ImageSegment{.number = CollisionSegment, .bytes = writer.getBytes()});
    return drawing;
}

} // namespace toki::scene
