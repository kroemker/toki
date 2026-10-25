#include "Collision.h"

#include "app/scene/Types.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize HeaderSize = 0x2C;
constexpr usize VertexSize = 6;
constexpr usize PolygonSize = 0x10;
constexpr usize SurfaceTypeSize = 8;
constexpr usize CameraSize = 8;
constexpr usize WaterBoxSize = 0x10;
constexpr u16 VertexIndexMask = 0x1FFF;
constexpr u16 VertexFlagsShift = 13;
constexpr f32 NormalOne = 32767.0F;
constexpr f32 FloorNormalY = 0.5F;
constexpr f32 CeilingNormalY = -0.8F;
constexpr f32 NormalTolerance = 0.98F;
constexpr usize NamedPolygons = 8;

[[nodiscard]] std::array<s16, 3> readVector(const std::span<const u8> bytes, const usize offset) {
    return {n64::readS16(bytes, offset), n64::readS16(bytes, offset + 2),
            n64::readS16(bytes, offset + 4)};
}

[[nodiscard]] u32 readBits(const u32 word, const u32 shift, const u32 mask) {
    return (word >> shift) & mask;
}

/// Polygons with one problem, named in one diagnostic.
struct PolygonProblem {
    std::string what;
    std::vector<usize> polygons;
};

[[nodiscard]] std::string listPolygons(const std::vector<usize>& polygons) {
    std::string text;
    for (usize i = 0; i < polygons.size() && i < NamedPolygons; ++i) {
        text += std::format("{}{}", i == 0 ? "" : ", ", polygons[i]);
    }
    if (polygons.size() > NamedPolygons) {
        text += std::format(" and {} more", polygons.size() - NamedPolygons);
    }
    return text;
}

class CollisionReader {
public:
    CollisionReader(const z64::MemoryImage& image, Collision& collision)
        : image(image), collision(collision) {}

    void read();

private:
    [[nodiscard]] std::optional<std::span<const u8>>
    findArray(u32 address, usize count, usize elementSize, std::string_view what);

    [[nodiscard]] usize findUnsizedCount(u32 address, usize elementSize, usize used,
                                         std::string_view what);

    void readVertices(u32 address, usize count);
    void readPolygons(u32 address, usize count);
    void readSurfaceTypes(u32 address);
    void readCameras(u32 address);
    void readWaterBoxes(u32 address, usize count);
    void checkPolygons();

    void report(const PolygonProblem& problem) {
        if (!problem.polygons.empty()) {
            collision.diagnostics.push_back(base::makeWarning(
                {}, std::format("the collision at 0x{:08X} has {} {}: {}", collision.address,
                                problem.polygons.size() == 1 ? "a polygon that" : "polygons that",
                                problem.what, listPolygons(problem.polygons))));
        }
    }

    const z64::MemoryImage& image;
    Collision& collision;
};

void CollisionReader::read() {
    const std::optional<std::span<const u8>> header =
        image.findBytes(collision.address, HeaderSize);
    if (!header) {
        collision.diagnostics.push_back(
            base::makeError({}, std::format("the collision header at 0x{:08X} is outside the image",
                                            collision.address)));
        return;
    }
    collision.minBounds = readVector(*header, 0x00);
    collision.maxBounds = readVector(*header, 0x06);
    readVertices(n64::readU32(*header, 0x10), n64::readU16(*header, 0x0C));
    readPolygons(n64::readU32(*header, 0x18), n64::readU16(*header, 0x14));
    readWaterBoxes(n64::readU32(*header, 0x28), n64::readU16(*header, 0x24));
    readSurfaceTypes(n64::readU32(*header, 0x1C));
    readCameras(n64::readU32(*header, 0x20));
    checkPolygons();
}

std::optional<std::span<const u8>> CollisionReader::findArray(const u32 address, const usize count,
                                                              const usize elementSize,
                                                              const std::string_view what) {
    if (count == 0) {
        return std::span<const u8>{};
    }
    const std::optional<std::span<const u8>> bytes = image.findBytes(address, count * elementSize);
    if (!bytes) {
        collision.diagnostics.push_back(base::makeError(
            {}, std::format("the collision's {} at 0x{:08X} ({} entries) is outside the image",
                            what, address, count)));
    }
    return bytes;
}

/// How long a list without a count is: the array laid out at its address, or,
/// where no symbol starts there, as many entries as the collision uses.
usize CollisionReader::findUnsizedCount(const u32 address, const usize elementSize,
                                        const usize used, const std::string_view what) {
    if (address == 0) {
        return 0;
    }
    if (const std::optional<z64::ImageSymbol> list = image.findSymbolAt(address)) {
        return list->size / elementSize;
    }
    collision.diagnostics.push_back(base::makeWarning(
        {}, std::format("the collision's {} at 0x{:08X} is no symbol, so only the {} entries the "
                        "collision uses are read",
                        what, address, used)));
    return used;
}

void CollisionReader::readVertices(const u32 address, const usize count) {
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, VertexSize, "vertex list");
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        collision.vertices.push_back(readVector(*bytes, i * VertexSize));
    }
}

void CollisionReader::readPolygons(const u32 address, const usize count) {
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, PolygonSize, "polygon list");
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * PolygonSize, PolygonSize);
        collision.polygons.push_back(CollisionPolygon{
            .type = n64::readU16(entry, 0),
            .vertexData = {n64::readU16(entry, 2), n64::readU16(entry, 4), n64::readU16(entry, 6)},
            .normal = readVector(entry, 8),
            .distance = n64::readS16(entry, 14)});
    }
}

void CollisionReader::readSurfaceTypes(const u32 address) {
    usize used = 0;
    for (const CollisionPolygon& polygon : collision.polygons) {
        used = std::max(used, usize{polygon.type} + 1);
    }
    const usize count = findUnsizedCount(address, SurfaceTypeSize, used, "surface type list");
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, SurfaceTypeSize, "surface type list");
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        collision.surfaceTypes.push_back(
            SurfaceType{.data = {n64::readU32(*bytes, i * SurfaceTypeSize),
                                 n64::readU32(*bytes, (i * SurfaceTypeSize) + 4)}});
    }
}

void CollisionReader::readCameras(const u32 address) {
    usize used = 0;
    for (const SurfaceType& surface : collision.surfaceTypes) {
        used = std::max(used, usize{readSurfaceFields(surface).bgCamIndex} + 1);
    }
    for (const WaterBox& box : collision.waterBoxes) {
        used = std::max(used, usize{getWaterBoxCamera(box)} + 1);
    }
    const usize count = findUnsizedCount(address, CameraSize, used, "camera list");
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, CameraSize, "camera list");
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * CameraSize, CameraSize);
        collision.cameras.push_back(CollisionCamera{.setting = n64::readU16(entry, 0),
                                                    .count = n64::readS16(entry, 2),
                                                    .data = n64::readU32(entry, 4)});
    }
}

void CollisionReader::readWaterBoxes(const u32 address, const usize count) {
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, WaterBoxSize, "waterbox list");
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * WaterBoxSize, WaterBoxSize);
        collision.waterBoxes.push_back(WaterBox{.xMin = n64::readS16(entry, 0),
                                                .ySurface = n64::readS16(entry, 2),
                                                .zMin = n64::readS16(entry, 4),
                                                .xLength = n64::readS16(entry, 6),
                                                .zLength = n64::readS16(entry, 8),
                                                .properties = n64::readU32(entry, 12)});
    }
}

/// Whether the normal the polygon stores is the one its corners make, in the
/// order the game winds them.
[[nodiscard]] bool hasOwnNormal(const CollisionPolygon& polygon,
                                const std::vector<std::array<s16, 3>>& vertices) {
    std::array<std::array<f32, 3>, 3> corners{};
    for (usize corner = 0; corner < 3; ++corner) {
        for (usize axis = 0; axis < 3; ++axis) {
            corners[corner][axis] =
                static_cast<f32>(vertices[getVertexIndex(polygon, corner)][axis]);
        }
    }
    std::array<f32, 3> first{};
    std::array<f32, 3> second{};
    for (usize axis = 0; axis < 3; ++axis) {
        first[axis] = corners[1][axis] - corners[0][axis];
        second[axis] = corners[2][axis] - corners[0][axis];
    }
    const std::array<f32, 3> cross{(first[1] * second[2]) - (first[2] * second[1]),
                                   (first[2] * second[0]) - (first[0] * second[2]),
                                   (first[0] * second[1]) - (first[1] * second[0])};
    const f32 length =
        std::sqrt((cross[0] * cross[0]) + (cross[1] * cross[1]) + (cross[2] * cross[2]));
    if (length == 0.0F) {
        return false;
    }
    f32 dot = 0.0F;
    for (usize axis = 0; axis < 3; ++axis) {
        dot += (cross[axis] / length) * (static_cast<f32>(polygon.normal[axis]) / NormalOne);
    }
    return dot >= NormalTolerance;
}

void CollisionReader::checkPolygons() {
    PolygonProblem vertex{.what = std::format("name a vertex past the {} the collision holds",
                                              collision.vertices.size())};
    PolygonProblem surface{.what =
                               std::format("name a surface type past the {} the collision holds",
                                           collision.surfaceTypes.size())};
    PolygonProblem normal{.what = "store a normal their corners do not make"};
    for (usize i = 0; i < collision.polygons.size(); ++i) {
        const CollisionPolygon& polygon = collision.polygons[i];
        if (polygon.type >= collision.surfaceTypes.size()) {
            surface.polygons.push_back(i);
        }
        const bool hasVertices =
            std::ranges::all_of(std::array<usize, 3>{0, 1, 2}, [&](const usize corner) {
                return getVertexIndex(polygon, corner) < collision.vertices.size();
            });
        if (!hasVertices) {
            vertex.polygons.push_back(i);
        } else if (!hasOwnNormal(polygon, collision.vertices)) {
            normal.polygons.push_back(i);
        }
    }
    report(vertex);
    report(surface);
    report(normal);
}

} // namespace

Collision readCollision(const z64::MemoryImage& image, const u32 address) {
    Collision collision{.address = address};
    CollisionReader(image, collision).read();
    return collision;
}

u16 getVertexIndex(const CollisionPolygon& polygon, const usize corner) {
    return polygon.vertexData[corner] & VertexIndexMask;
}

u8 getVertexFlags(const CollisionPolygon& polygon, const usize corner) {
    return static_cast<u8>(polygon.vertexData[corner] >> VertexFlagsShift);
}

PolygonKind findPolygonKind(const CollisionPolygon& polygon) {
    const f32 y = static_cast<f32>(polygon.normal[1]) / NormalOne;
    if (y > FloorNormalY) {
        return PolygonKind::Floor;
    }
    if (y < CeilingNormalY) {
        return PolygonKind::Ceiling;
    }
    return PolygonKind::Wall;
}

SurfaceFields readSurfaceFields(const SurfaceType& surface) {
    const u32 first = surface.data[0];
    const u32 second = surface.data[1];
    return SurfaceFields{
        .bgCamIndex = static_cast<u8>(readBits(first, 0, 0xFF)),
        .exitIndex = static_cast<u8>(readBits(first, 8, 0x1F)),
        .floorType = static_cast<u8>(readBits(first, 13, 0x1F)),
        .unk18 = static_cast<u8>(readBits(first, 18, 0x07)),
        .wallType = static_cast<u8>(readBits(first, 21, 0x1F)),
        .floorProperty = static_cast<u8>(readBits(first, 26, 0x0F)),
        .isSoft = readBits(first, 30, 1) != 0,
        .isHorseBlocked = readBits(first, 31, 1) != 0,
        .material = static_cast<u8>(readBits(second, 0, 0x0F)),
        .floorEffect = static_cast<u8>(readBits(second, 4, 0x03)),
        .lightSetting = static_cast<u8>(readBits(second, 6, 0x1F)),
        .echo = static_cast<u8>(readBits(second, 11, 0x3F)),
        .canHookshot = readBits(second, 17, 1) != 0,
        .conveyorSpeed = static_cast<u8>(readBits(second, 18, 0x07)),
        .conveyorDirection = static_cast<u8>(readBits(second, 21, 0x3F)),
        .unk27 = readBits(second, 27, 1) != 0,
    };
}

std::vector<usize> countSurfacePolygons(const Collision& collision) {
    std::vector<usize> counts(collision.surfaceTypes.size(), 0);
    for (const CollisionPolygon& polygon : collision.polygons) {
        if (polygon.type < counts.size()) {
            ++counts[polygon.type];
        }
    }
    return counts;
}

u8 getWaterBoxCamera(const WaterBox& box) {
    return static_cast<u8>(readBits(box.properties, 0, 0xFF));
}

u8 getWaterBoxLightSetting(const WaterBox& box) {
    return static_cast<u8>(readBits(box.properties, 8, 0x1F));
}

u8 getWaterBoxRoom(const WaterBox& box) {
    return static_cast<u8>(readBits(box.properties, 13, 0x3F));
}

} // namespace toki::scene
