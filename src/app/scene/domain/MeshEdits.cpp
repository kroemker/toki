#include "MeshEdits.h"

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
constexpr f32 NormalOne = 32767.0F;
constexpr f32 VtxNormalOne = 127.0F;
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
                const std::array<u8, 3> normal = makeVtxNormal(entry.transform, sum);
                writes.push_back({.symbol = entry.key.symbol,
                                  .offset = (entry.key.index * VtxSize) + VtxNormalOffset,
                                  .bytes = {normal.begin(), normal.end()}});
            }
        }
    }

    void writeBounds(const Scene& scene, const usize layer) {
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
            if (std::optional<SymbolWrite> bounds =
                    makePartBoundsWrite(scene, layer, room.room, part, points)) {
                writes.push_back(std::move(*bounds));
            }
        }
    }

private:
    const RoomMesh& room;
    std::vector<SymbolWrite>& writes;
    std::vector<glm::vec3> positions;
    std::set<usize> movedVertices;
    std::vector<usize> changedFaces;
};

} // namespace

std::optional<SymbolWrite> makePartBoundsWrite(const Scene& scene, const usize layer,
                                               const usize room, const usize part,
                                               const std::span<const glm::vec3> points) {
    if (points.empty()) {
        return std::nullopt;
    }
    const std::optional<std::string> entries = findCullableEntries(scene, layer, room);
    if (!entries) {
        return std::nullopt;
    }
    return SymbolWrite{.symbol = *entries,
                       .offset = part * CullableEntrySize,
                       .bytes = makeSphereBytes({points.begin(), points.end()})};
}

std::array<u8, 3> makeVtxNormal(const glm::mat4& transform, const glm::vec3& facing) {
    const glm::vec3 local = glm::normalize(glm::transpose(glm::mat3(transform)) * facing);
    std::array<u8, 3> bytes{};
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        bytes[static_cast<usize>(axis)] = static_cast<u8>(
            static_cast<s8>(std::clamp(std::lround(local[axis] * VtxNormalOne), -128L, 127L)));
    }
    return bytes;
}

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
        writer.writeBounds(scene, mesh.layer);
    }
    return writes;
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

SceneMesh makeMovedMesh(const SceneMesh& mesh, const std::span<const MeshVertexMove> moves,
                        const std::span<const SymbolWrite> writes) {
    SceneMesh moved = mesh;
    std::map<std::string, std::vector<const SymbolWrite*>, std::less<>> written;
    for (const SymbolWrite& write : writes) {
        written[write.symbol].push_back(&write);
    }
    for (RoomMesh& room : moved.rooms) {
        for (MeshEntry& entry : room.entries) {
            const auto found = written.find(entry.key.symbol);
            if (found == written.end()) {
                continue;
            }
            const usize start = entry.key.index * VtxSize;
            for (const SymbolWrite* write : found->second) {
                const usize from = std::max(start, write->offset);
                const usize to = std::min(start + VtxSize, write->offset + write->bytes.size());
                for (usize at = from; at < to; ++at) {
                    entry.vtx[at - start] = write->bytes[at - write->offset];
                }
            }
        }
    }
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
