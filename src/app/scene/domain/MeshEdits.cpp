#include "MeshEdits.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"
#include "z64/SymbolData.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize VtxSize = 16;
constexpr usize VtxNormalOffset = 12;
constexpr usize CullableEntrySize = 0x10;
constexpr u8 RoomShapeCode = 0x0A;
constexpr usize RoomShapeEntriesOffset = 4;
constexpr usize CollisionVerticesOffset = 0x10;
constexpr usize CollisionPolygonsOffset = 0x18;
constexpr usize CollisionVertexSize = 6;
constexpr usize PolygonSize = 0x10;
constexpr usize PolygonNormalOffset = 8;
constexpr f32 NormalOne = 32767.0F;
constexpr f32 VtxNormalOne = 127.0F;
constexpr f32 SamePlace = 0.5F;
constexpr f32 Flat = 1.0e-6F;

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

void addHalf(std::vector<u8>& bytes, const s16 value) {
    bytes.push_back(static_cast<u8>(static_cast<u16>(value) >> 8));
    bytes.push_back(static_cast<u8>(value));
}

[[nodiscard]] std::array<s16, 3> makeStored(const MeshEntry& entry, const glm::vec3& world) {
    const glm::vec4 local = glm::inverse(entry.transform) * glm::vec4(world, 1.0F);
    return {roundToS16(local.x), roundToS16(local.y), roundToS16(local.z)};
}

[[nodiscard]] glm::vec3 findWorld(const MeshEntry& entry, const std::array<s16, 3>& stored) {
    return glm::vec3(entry.transform * glm::vec4(static_cast<f32>(stored[0]),
                                                 static_cast<f32>(stored[1]),
                                                 static_cast<f32>(stored[2]), 1.0F));
}

/// The name of the symbol that starts at `address` in `image`, if one does.
[[nodiscard]] std::optional<std::string> findNameAt(const z64::MemoryImage& image,
                                                    const u32 address) {
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.segment == (address >> 24) && symbol.offset == (address & 0x00FF'FFFFU)) {
            return name;
        }
    }
    return std::nullopt;
}

/// The name of the entry list of a cullable room's shape, if `room` is one.
[[nodiscard]] std::optional<std::string> findCullableEntries(const Scene& scene, const usize layer,
                                                             const usize room) {
    if (layer >= scene.layers.size() || room >= scene.layers[layer].rooms.size() ||
        FirstRoomImage + room >= scene.images.size()) {
        return std::nullopt;
    }
    const RoomHeader& header = scene.layers[layer].rooms[room];
    if (header.shapeType != RoomShapeType::Cullable) {
        return std::nullopt;
    }
    const auto command = std::ranges::find(header.commands, RoomShapeCode, &HeaderCommand::code);
    if (command == header.commands.end()) {
        return std::nullopt;
    }
    const z64::MemoryImage& image = *scene.images[FirstRoomImage + room];
    const std::optional<std::span<const u8>> shape =
        image.findBytes(command->data2, RoomShapeEntriesOffset + 4);
    if (!shape) {
        return std::nullopt;
    }
    return findNameAt(image, n64::readU32(*shape, RoomShapeEntriesOffset));
}

[[nodiscard]] std::vector<u8> makeSphereBytes(const std::vector<glm::vec3>& points) {
    glm::vec3 lowest = points.front();
    glm::vec3 highest = points.front();
    for (const glm::vec3& point : points) {
        lowest = glm::min(lowest, point);
        highest = glm::max(highest, point);
    }
    const glm::vec3 center = (lowest + highest) / 2.0F;
    const std::array<s16, 3> stored{roundToS16(center.x), roundToS16(center.y),
                                    roundToS16(center.z)};
    const glm::vec3 rounded(stored[0], stored[1], stored[2]);
    f32 radius = 0.0F;
    for (const glm::vec3& point : points) {
        radius = std::max(radius, glm::length(point - rounded));
    }
    std::vector<u8> bytes;
    for (const s16 axis : stored) {
        addHalf(bytes, axis);
    }
    addHalf(bytes, roundToS16(std::ceil(radius)));
    return bytes;
}

class RoomMoveWriter {
public:
    RoomMoveWriter(const RoomMesh& room, std::vector<SymbolWrite>& writes)
        : room(room), writes(writes) {
        for (const MeshVertex& vertex : room.vertices) {
            positions.push_back(vertex.position);
        }
    }

    void move(const MeshVertexMove& moved) {
        if (moved.vertex < positions.size()) {
            positions[moved.vertex] = moved.position;
            movedVertices.insert(moved.vertex);
        }
    }

    void writePositions() {
        for (const usize vertex : movedVertices) {
            for (const usize index : room.vertices[vertex].entries) {
                const MeshEntry& entry = room.entries[index];
                std::vector<u8> bytes;
                for (const s16 axis : makeStored(entry, positions[vertex])) {
                    addHalf(bytes, axis);
                }
                writes.push_back({.symbol = entry.key.symbol,
                                  .offset = entry.key.index * VtxSize,
                                  .bytes = std::move(bytes)});
            }
        }
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (std::ranges::any_of(room.faces[face].vertices, [this](const usize vertex) {
                    return movedVertices.contains(vertex);
                })) {
                changedFaces.push_back(face);
            }
        }
    }

    void writeNormals() {
        std::set<usize> turned;
        for (const usize face : changedFaces) {
            turned.insert(room.faces[face].vertices.begin(), room.faces[face].vertices.end());
        }
        std::map<usize, glm::vec3> sums;
        for (const MeshFace& face : room.faces) {
            const glm::vec3 facing =
                glm::cross(positions[face.vertices[1]] - positions[face.vertices[0]],
                           positions[face.vertices[2]] - positions[face.vertices[0]]);
            for (const usize vertex : face.vertices) {
                if (turned.contains(vertex)) {
                    sums[vertex] += facing;
                }
            }
        }
        for (const auto& [vertex, sum] : sums) {
            if (glm::length(sum) < Flat) {
                continue;
            }
            for (const usize index : room.vertices[vertex].entries) {
                const MeshEntry& entry = room.entries[index];
                if (!entry.isLit) {
                    continue;
                }
                const glm::vec3 local =
                    glm::normalize(glm::transpose(glm::mat3(entry.transform)) * sum);
                std::vector<u8> bytes;
                for (glm::length_t axis = 0; axis < 3; ++axis) {
                    bytes.push_back(static_cast<u8>(static_cast<s8>(
                        std::clamp(std::lround(local[axis] * VtxNormalOne), -128L, 127L))));
                }
                writes.push_back({.symbol = entry.key.symbol,
                                  .offset = (entry.key.index * VtxSize) + VtxNormalOffset,
                                  .bytes = std::move(bytes)});
            }
        }
    }

    void writeBounds(const std::string& entries) {
        std::set<usize> parts;
        for (const usize face : changedFaces) {
            parts.insert(room.faces[face].part);
        }
        for (const usize part : parts) {
            std::vector<glm::vec3> points;
            for (const MeshFace& face : room.faces) {
                if (face.part == part) {
                    for (const usize vertex : face.vertices) {
                        points.push_back(positions[vertex]);
                    }
                }
            }
            writes.push_back({.symbol = entries,
                              .offset = part * CullableEntrySize,
                              .bytes = makeSphereBytes(points)});
        }
    }

private:
    const RoomMesh& room;
    std::vector<SymbolWrite>& writes;
    std::vector<glm::vec3> positions;
    std::set<usize> movedVertices;
    std::vector<usize> changedFaces;
};

[[nodiscard]] bool isAtSamePlace(const glm::vec3& first, const glm::vec3& second) {
    return std::abs(first.x - second.x) <= SamePlace && std::abs(first.y - second.y) <= SamePlace &&
           std::abs(first.z - second.z) <= SamePlace;
}

[[nodiscard]] glm::vec3 makePoint(const std::array<s16, 3>& position) {
    return {position[0], position[1], position[2]};
}

/// The normal and distance of `polygon` as its corners in `vertices` make
/// them, or nothing when they are in one line.
[[nodiscard]] std::optional<CollisionPolygon>
makeFacedPolygon(CollisionPolygon polygon, const std::vector<std::array<s16, 3>>& vertices) {
    std::array<glm::vec3, 3> corners{};
    for (usize corner = 0; corner < 3; ++corner) {
        const u16 index = getVertexIndex(polygon, corner);
        if (index >= vertices.size()) {
            return std::nullopt;
        }
        corners[corner] = makePoint(vertices[index]);
    }
    const glm::vec3 facing = glm::cross(corners[1] - corners[0], corners[2] - corners[0]);
    if (glm::length(facing) < Flat) {
        return std::nullopt;
    }
    const glm::vec3 unit = glm::normalize(facing);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        polygon.normal[static_cast<usize>(axis)] = roundToS16(unit[axis] * NormalOne);
    }
    polygon.distance = roundToS16(-glm::dot(unit, corners[0]));
    return polygon;
}

} // namespace

std::vector<SymbolWrite> makeMeshMoveWrites(const Scene& scene, const SceneMesh& mesh,
                                            const std::span<const MeshVertexMove> moves) {
    std::vector<SymbolWrite> writes;
    for (const RoomMesh& room : mesh.rooms) {
        RoomMoveWriter writer(room, writes);
        bool isMoved = false;
        for (const MeshVertexMove& moved : moves) {
            if (moved.room == room.room) {
                writer.move(moved);
                isMoved = true;
            }
        }
        if (!isMoved) {
            continue;
        }
        writer.writePositions();
        writer.writeNormals();
        if (const std::optional<std::string> entries =
                findCullableEntries(scene, mesh.layer, room.room)) {
            writer.writeBounds(*entries);
        }
    }
    return writes;
}

Collision makeMovedCollision(const Collision& collision,
                             const std::span<const CollisionVertexMove> moves) {
    Collision moved = collision;
    std::set<usize> movedVertices;
    for (const CollisionVertexMove& move : moves) {
        if (move.vertex < moved.vertices.size()) {
            moved.vertices[move.vertex] = move.position;
            movedVertices.insert(move.vertex);
        }
    }
    for (CollisionPolygon& polygon : moved.polygons) {
        const bool isTouched =
            std::ranges::any_of(std::array<usize, 3>{0, 1, 2}, [&](const usize corner) {
                return movedVertices.contains(getVertexIndex(polygon, corner));
            });
        if (!isTouched) {
            continue;
        }
        if (const std::optional<CollisionPolygon> faced =
                makeFacedPolygon(polygon, moved.vertices)) {
            polygon = *faced;
        }
    }
    if (!moved.vertices.empty()) {
        moved.minBounds = moved.vertices.front();
        moved.maxBounds = moved.vertices.front();
        for (const std::array<s16, 3>& vertex : moved.vertices) {
            for (usize axis = 0; axis < 3; ++axis) {
                moved.minBounds[axis] = std::min(moved.minBounds[axis], vertex[axis]);
                moved.maxBounds[axis] = std::max(moved.maxBounds[axis], vertex[axis]);
            }
        }
    }
    return moved;
}

std::vector<SymbolWrite> makeCollisionMoveWrites(const Scene& scene, const Collision& collision,
                                                 const std::span<const CollisionVertexMove> moves) {
    std::vector<SymbolWrite> writes;
    if (moves.empty() || scene.images.size() <= SceneImage) {
        return writes;
    }
    const z64::MemoryImage& image = *scene.images[SceneImage];
    const std::optional<std::string> header = findNameAt(image, collision.address);
    const std::optional<std::span<const u8>> headerBytes =
        image.findBytes(collision.address, CollisionPolygonsOffset + 4);
    if (!header || !headerBytes) {
        return writes;
    }
    const std::optional<std::string> vertices =
        findNameAt(image, n64::readU32(*headerBytes, CollisionVerticesOffset));
    const std::optional<std::string> polygons =
        findNameAt(image, n64::readU32(*headerBytes, CollisionPolygonsOffset));
    if (!vertices || !polygons) {
        return writes;
    }
    const Collision moved = makeMovedCollision(collision, moves);
    for (const CollisionVertexMove& move : moves) {
        std::vector<u8> bytes;
        for (const s16 axis : moved.vertices[move.vertex]) {
            addHalf(bytes, axis);
        }
        writes.push_back({.symbol = *vertices,
                          .offset = move.vertex * CollisionVertexSize,
                          .bytes = std::move(bytes)});
    }
    for (usize i = 0; i < moved.polygons.size(); ++i) {
        const CollisionPolygon& polygon = moved.polygons[i];
        if (polygon == collision.polygons[i]) {
            continue;
        }
        std::vector<u8> bytes;
        for (const s16 axis : polygon.normal) {
            addHalf(bytes, axis);
        }
        addHalf(bytes, polygon.distance);
        writes.push_back({.symbol = *polygons,
                          .offset = (i * PolygonSize) + PolygonNormalOffset,
                          .bytes = std::move(bytes)});
    }
    std::vector<u8> bounds;
    for (const s16 axis : moved.minBounds) {
        addHalf(bounds, axis);
    }
    for (const s16 axis : moved.maxBounds) {
        addHalf(bounds, axis);
    }
    writes.push_back({.symbol = *header, .offset = 0, .bytes = std::move(bounds)});
    return writes;
}

std::vector<CollisionVertexMove>
findLinkedCollisionMoves(const Collision& collision, const SceneMesh& mesh,
                         const std::span<const MeshVertexMove> moves) {
    std::map<usize, CollisionVertexMove> linked;
    for (const MeshVertexMove& move : moves) {
        const RoomMesh* room = findRoomMesh(mesh, move.room);
        if (room == nullptr || move.vertex >= room->vertices.size()) {
            continue;
        }
        const glm::vec3& before = room->vertices[move.vertex].position;
        for (usize vertex = 0; vertex < collision.vertices.size(); ++vertex) {
            if (isAtSamePlace(makePoint(collision.vertices[vertex]), before)) {
                linked[vertex] = {.vertex = vertex,
                                  .position = {roundToS16(move.position.x),
                                               roundToS16(move.position.y),
                                               roundToS16(move.position.z)}};
            }
        }
    }
    std::vector<CollisionVertexMove> found;
    for (const auto& [vertex, move] : linked) {
        found.push_back(move);
    }
    return found;
}

std::vector<MeshVertexMove> findLinkedMeshMoves(const Collision& collision, const SceneMesh& mesh,
                                                const std::span<const CollisionVertexMove> moves) {
    std::vector<MeshVertexMove> found;
    for (const CollisionVertexMove& move : moves) {
        if (move.vertex >= collision.vertices.size()) {
            continue;
        }
        const glm::vec3 before = makePoint(collision.vertices[move.vertex]);
        for (const RoomMesh& room : mesh.rooms) {
            for (usize vertex = 0; vertex < room.vertices.size(); ++vertex) {
                if (isAtSamePlace(room.vertices[vertex].position, before)) {
                    found.push_back({.room = room.room,
                                     .vertex = vertex,
                                     .position = makePoint(move.position)});
                }
            }
        }
    }
    return found;
}

std::expected<void, base::Diagnostic> applySymbolWrites(SceneModel& model,
                                                        const std::span<const SymbolWrite> writes) {
    std::map<std::string, z64::SymbolData> written;
    for (const SymbolWrite& write : writes) {
        auto found = written.find(write.symbol);
        if (found == written.end()) {
            const z64::SymbolData* symbol = model.findSymbol(write.symbol);
            if (symbol == nullptr) {
                return std::unexpected(base::makeError(
                    {}, std::format("the scene has no symbol '{}' to write", write.symbol)));
            }
            found = written.emplace(write.symbol, *symbol).first;
        }
        std::vector<u8>& bytes = found->second.bytes;
        if (write.offset + write.bytes.size() > bytes.size()) {
            return std::unexpected(base::makeError(
                {}, std::format("'{}' is {} bytes long, too short to write {} bytes at {}",
                                write.symbol, bytes.size(), write.bytes.size(), write.offset)));
        }
        std::ranges::copy(write.bytes, bytes.begin() + static_cast<std::ptrdiff_t>(write.offset));
    }
    for (auto& [name, symbol] : written) {
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(symbol)); !set) {
            return set;
        }
    }
    return {};
}

std::vector<std::shared_ptr<const z64::MemoryImage>>
makeWrittenImages(const Scene& scene, const std::span<const SymbolWrite> writes) {
    std::vector<std::shared_ptr<const z64::MemoryImage>> images = scene.images;
    std::map<usize, std::shared_ptr<z64::MemoryImage>> copies;
    for (const SymbolWrite& write : writes) {
        for (usize index = SceneImage; index < images.size(); ++index) {
            const auto found = scene.images[index]->symbols.find(write.symbol);
            if (found == scene.images[index]->symbols.end()) {
                continue;
            }
            auto copy = copies.find(index);
            if (copy == copies.end()) {
                copy =
                    copies.emplace(index, std::make_shared<z64::MemoryImage>(*scene.images[index]))
                        .first;
                images[index] = copy->second;
            }
            for (z64::ImageSegment& segment : copy->second->segments) {
                const usize at = found->second.offset + write.offset;
                if (segment.number == found->second.segment &&
                    at + write.bytes.size() <= segment.bytes.size()) {
                    std::ranges::copy(write.bytes,
                                      segment.bytes.begin() + static_cast<std::ptrdiff_t>(at));
                }
            }
            break;
        }
    }
    return images;
}

bool hasSymbolWrites(const Scene& scene, const std::span<const SymbolWrite> writes) {
    for (const SymbolWrite& write : writes) {
        bool isFound = false;
        for (usize index = SceneImage; index < scene.images.size() && !isFound; ++index) {
            const z64::MemoryImage& image = *scene.images[index];
            const auto found = image.symbols.find(write.symbol);
            if (found == image.symbols.end()) {
                continue;
            }
            const u32 address = (u32{found->second.segment} << 24) |
                                static_cast<u32>(found->second.offset + write.offset);
            const std::optional<std::span<const u8>> bytes =
                image.findBytes(address, write.bytes.size());
            if (!bytes || !std::ranges::equal(*bytes, write.bytes)) {
                return false;
            }
            isFound = true;
        }
        if (!isFound) {
            return false;
        }
    }
    return true;
}

SceneMesh makeMovedMesh(const SceneMesh& mesh, const std::span<const MeshVertexMove> moves) {
    SceneMesh moved = mesh;
    for (const MeshVertexMove& move : moves) {
        const auto room = std::ranges::find(moved.rooms, move.room, &RoomMesh::room);
        if (room == moved.rooms.end() || move.vertex >= room->vertices.size()) {
            continue;
        }
        MeshVertex& vertex = room->vertices[move.vertex];
        for (const usize index : vertex.entries) {
            MeshEntry& entry = room->entries[index];
            entry.stored = makeStored(entry, move.position);
        }
        if (!vertex.entries.empty()) {
            const MeshEntry& first = room->entries[vertex.entries.front()];
            vertex.position = findWorld(first, first.stored);
        }
    }
    return moved;
}

} // namespace toki::scene
