#include "RoomMesh.h"

#include "app/scene/Types.h"
#include "app/scene/domain/RuntimeSegments.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "hackeroot/GameCode.h"
#include "n64/BigEndian.h"
#include "n64/FixedMatrix.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

constexpr u32 VtxSize = 16;
constexpr f32 PlaceSteps = 8.0F;

struct DrawPart {
    usize room = 0;
    usize part = 0;
    bool isTranslucent = false;
};

struct MeshContent {
    viewport::ViewportContent content;
    std::vector<std::optional<DrawPart>> parts;
};

/// What the viewport draws of every room of `layer`, with nothing else but
/// what the game sets up before them.
[[nodiscard]] MeshContent makeMeshContent(const Scene& scene, const SceneLayer& layer) {
    MeshContent mesh{.content = viewport::ViewportContent{.images = scene.images}};
    const auto addDraw = [&mesh](viewport::ViewportDraw draw, std::optional<DrawPart> part) {
        mesh.content.draws.push_back(std::move(draw));
        mesh.parts.push_back(part);
    };
    const hackeroot::GameSetup setup = hackeroot::findGameSetup(*scene.images.front());
    const RuntimeSegments runtime = makeRuntimeSegments();
    const usize standInImage = mesh.content.images.size();
    mesh.content.images.push_back(std::make_shared<const z64::MemoryImage>(runtime.image));
    if (setup.drawConfig) {
        addDraw({.image = 0, .displayList = *setup.drawConfig}, std::nullopt);
    }
    viewport::ViewportDraw standIn{.image = standInImage, .displayList = runtime.standIn};
    for (u8 segment = FirstRuntimeSegment; segment <= LastRuntimeSegment; ++segment) {
        standIn.pointedSegments.push_back(viewport::SegmentPointer{
            .segment = segment, .image = standInImage, .address = runtime.standIn});
    }
    addDraw(std::move(standIn), std::nullopt);
    if (setup.setupDisplayList) {
        addDraw({.image = 0, .displayList = *setup.setupDisplayList}, std::nullopt);
    }
    for (usize room = 0; room < layer.rooms.size() && FirstRoomImage + room < scene.images.size();
         ++room) {
        const std::vector<RoomDisplayLists>& lists = layer.rooms[room].displayLists;
        for (usize part = 0; part < lists.size(); ++part) {
            for (const bool isTranslucent : {false, true}) {
                const u32 list = isTranslucent ? lists[part].translucent : lists[part].opaque;
                if (list != 0) {
                    addDraw({.image = FirstRoomImage + room, .displayList = list},
                            DrawPart{.room = room, .part = part, .isTranslucent = isTranslucent});
                }
            }
        }
    }
    return mesh;
}

/// A `Vtx` or `Gfx` array where the frame mapped it.
struct MappedArray {
    u32 start = 0;
    u32 end = 0;
    usize image = 0;
    u8 segment = 0;
    usize offset = 0;
    const std::string* name = nullptr;
};

[[nodiscard]] std::vector<MappedArray>
listMappedArrays(const Scene& scene, const viewport::Frame& frame, const std::string_view type) {
    std::vector<MappedArray> arrays;
    for (usize image = SceneImage; image < scene.images.size(); ++image) {
        if (image >= frame.mappedImages.size() || !frame.mappedImages[image]) {
            continue;
        }
        for (const auto& [name, symbol] : scene.images[image]->symbols) {
            if (symbol.type != type) {
                continue;
            }
            const u32 start = frame.mappedImages[image]->segments[symbol.segment] +
                              static_cast<u32>(symbol.offset);
            arrays.push_back(MappedArray{.start = start,
                                         .end = start + static_cast<u32>(symbol.size),
                                         .image = image,
                                         .segment = symbol.segment,
                                         .offset = symbol.offset,
                                         .name = &name});
        }
    }
    std::ranges::sort(arrays, {}, &MappedArray::start);
    return arrays;
}

[[nodiscard]] const MappedArray* findMappedArray(const std::vector<MappedArray>& arrays,
                                                 const u32 address) {
    const auto after = std::ranges::upper_bound(arrays, address, {}, &MappedArray::start);
    if (after == arrays.begin()) {
        return nullptr;
    }
    const MappedArray& array = *std::prev(after);
    return address < array.end ? &array : nullptr;
}

[[nodiscard]] glm::mat4 makeTransform(const n64::FixedMatrix& matrix) {
    glm::mat4 transform(1.0F);
    for (usize row = 0; row < 4; ++row) {
        for (usize column = 0; column < 4; ++column) {
            transform[static_cast<int>(row)][static_cast<int>(column)] =
                matrix.getFloat(row, column);
        }
    }
    return transform;
}

[[nodiscard]] glm::vec3 findWorldPosition(const MeshEntry& entry) {
    const glm::vec4 world = entry.transform * glm::vec4(static_cast<f32>(entry.stored[0]),
                                                        static_cast<f32>(entry.stored[1]),
                                                        static_cast<f32>(entry.stored[2]), 1.0F);
    return glm::vec3(world);
}

/// Reads rooms' faces out of a frame, keeping each room's entries once.
class FaceReader {
public:
    FaceReader(const Scene& scene, const viewport::Frame& frame, const usize roomCount)
        : scene(scene), frame(frame), arrays(listMappedArrays(scene, frame, "Vtx")),
          lists(listMappedArrays(scene, frame, "Gfx")), rooms(roomCount), entryIndices(roomCount),
          leftOut(roomCount, 0) {
        for (usize room = 0; room < roomCount; ++room) {
            rooms[room].room = room;
        }
    }

    void readDraw(const viewport::OutputRange& range, const DrawPart& part) {
        for (usize output = range.first; output < range.end; ++output) {
            if (const auto* triangle =
                    std::get_if<n64::Triangle>(&frame.rspResult.outputs[output])) {
                readTriangle(*triangle, part);
            }
        }
    }

    [[nodiscard]] std::vector<RoomMesh> takeRooms() {
        return std::move(rooms);
    }

    [[nodiscard]] const std::vector<usize>& getLeftOut() const {
        return leftOut;
    }

private:
    void readTriangle(const n64::Triangle& triangle, const DrawPart& part) {
        MeshFace face{
            .part = part.part, .isTranslucent = part.isTranslucent, .isLit = triangle.lit};
        if (const MappedArray* list = findMappedArray(lists, triangle.address)) {
            face.draw = {.list = *list->name, .command = (triangle.address - list->start) / 8};
        }
        for (usize corner = 0; corner < 3; ++corner) {
            const std::optional<usize> entry = findEntry(part.room, triangle.sources[corner]);
            if (!entry) {
                ++leftOut[part.room];
                return;
            }
            face.corners[corner] = *entry;
        }
        for (const usize entry : face.corners) {
            rooms[part.room].entries[entry].isLit |= triangle.lit;
        }
        rooms[part.room].faces.push_back(face);
    }

    [[nodiscard]] std::optional<usize> findEntry(const usize room,
                                                 const n64::VertexSource& source) {
        const MappedArray* array = findMappedArray(arrays, source.address);
        if (array == nullptr || source.modelview >= frame.rspResult.modelviews.size()) {
            return std::nullopt;
        }
        MeshEntryKey key{.symbol = *array->name,
                         .index = (source.address - array->start) / VtxSize};
        std::map<MeshEntryKey, usize>& indices = entryIndices[room];
        if (const auto found = indices.find(key); found != indices.end()) {
            return found->second;
        }
        const usize offset = array->offset + (key.index * VtxSize);
        const std::optional<std::span<const u8>> bytes = scene.images[array->image]->findBytes(
            (u32{array->segment} << 24) | static_cast<u32>(offset), VtxSize);
        if (!bytes) {
            return std::nullopt;
        }
        std::vector<MeshEntry>& entries = rooms[room].entries;
        MeshEntry entry{
            .key = key,
            .image = array->image,
            .stored = {n64::readS16(*bytes, 0), n64::readS16(*bytes, 2), n64::readS16(*bytes, 4)},
            .transform = makeTransform(frame.rspResult.modelviews[source.modelview])};
        std::ranges::copy(*bytes, entry.vtx.begin());
        entries.push_back(entry);
        indices.emplace(std::move(key), entries.size() - 1);
        return entries.size() - 1;
    }

    const Scene& scene;
    const viewport::Frame& frame;
    std::vector<MappedArray> arrays;
    std::vector<MappedArray> lists;
    std::vector<RoomMesh> rooms;
    std::vector<std::map<MeshEntryKey, usize>> entryIndices;
    std::vector<usize> leftOut;
};

class UnionFind {
public:
    explicit UnionFind(const usize size) : parents(size) {
        std::iota(parents.begin(), parents.end(), usize{0});
    }

    [[nodiscard]] usize findRoot(usize item) {
        while (parents[item] != item) {
            parents[item] = parents[parents[item]];
            item = parents[item];
        }
        return item;
    }

    void join(const usize first, const usize second) {
        parents[findRoot(first)] = findRoot(second);
    }

private:
    std::vector<usize> parents;
};

using PlaceKey = std::array<s64, 3>;

[[nodiscard]] PlaceKey makePlaceKey(const glm::vec3& position) {
    return {std::llround(position.x * PlaceSteps), std::llround(position.y * PlaceSteps),
            std::llround(position.z * PlaceSteps)};
}

/// Joins the entries `kept` grouped into one vertex, then, among entries it
/// does not have, those at one place on faces that share an edge there.
void joinEntries(const RoomMesh& mesh, const RoomMesh* kept, UnionFind& groups) {
    std::vector<bool> isKept(mesh.entries.size(), false);
    if (kept != nullptr) {
        std::map<MeshEntryKey, usize> keptVertices;
        for (usize vertex = 0; vertex < kept->vertices.size(); ++vertex) {
            for (const usize entry : kept->vertices[vertex].entries) {
                keptVertices.emplace(kept->entries[entry].key, vertex);
            }
        }
        std::map<usize, usize> firstOfVertex;
        for (usize entry = 0; entry < mesh.entries.size(); ++entry) {
            const auto found = keptVertices.find(mesh.entries[entry].key);
            if (found == keptVertices.end()) {
                continue;
            }
            isKept[entry] = true;
            const auto [first, isNew] = firstOfVertex.emplace(found->second, entry);
            if (!isNew) {
                groups.join(entry, first->second);
            }
        }
    }

    std::vector<PlaceKey> places;
    places.reserve(mesh.entries.size());
    for (const MeshEntry& entry : mesh.entries) {
        places.push_back(makePlaceKey(findWorldPosition(entry)));
    }
    std::map<std::pair<PlaceKey, PlaceKey>, std::array<usize, 2>> edges;
    for (const MeshFace& face : mesh.faces) {
        for (usize corner = 0; corner < 3; ++corner) {
            std::array<usize, 2> ends{face.corners[corner], face.corners[(corner + 1) % 3]};
            if (isKept[ends[0]] || isKept[ends[1]] || places[ends[0]] == places[ends[1]]) {
                continue;
            }
            if (places[ends[1]] < places[ends[0]]) {
                std::swap(ends[0], ends[1]);
            }
            const auto [found, isNew] =
                edges.emplace(std::pair{places[ends[0]], places[ends[1]]}, ends);
            if (!isNew) {
                groups.join(ends[0], found->second[0]);
                groups.join(ends[1], found->second[1]);
            }
        }
    }
}

void groupVertices(RoomMesh& mesh, const RoomMesh* kept) {
    UnionFind groups(mesh.entries.size());
    joinEntries(mesh, kept, groups);
    std::map<usize, usize> vertexOfRoot;
    std::vector<usize> vertexOfEntry(mesh.entries.size());
    for (usize entry = 0; entry < mesh.entries.size(); ++entry) {
        const auto [found, isNew] =
            vertexOfRoot.emplace(groups.findRoot(entry), mesh.vertices.size());
        if (isNew) {
            mesh.vertices.push_back(MeshVertex{.position = findWorldPosition(mesh.entries[entry])});
        }
        mesh.vertices[found->second].entries.push_back(entry);
        vertexOfEntry[entry] = found->second;
    }
    std::set<MeshEdge> edges;
    for (MeshFace& face : mesh.faces) {
        for (usize corner = 0; corner < 3; ++corner) {
            face.vertices[corner] = vertexOfEntry[face.corners[corner]];
        }
        for (usize corner = 0; corner < 3; ++corner) {
            const usize from = face.vertices[corner];
            const usize to = face.vertices[(corner + 1) % 3];
            if (from != to) {
                edges.insert(MeshEdge{.vertices = {std::min(from, to), std::max(from, to)}});
            }
        }
    }
    mesh.edges.assign(edges.begin(), edges.end());
}

} // namespace

SceneMesh readSceneMesh(const Scene& scene, const usize layer, const SceneMesh* kept) {
    SceneMesh mesh{.layer = layer};
    if (layer >= scene.layers.size() || scene.images.size() < FirstRoomImage) {
        return mesh;
    }
    const SceneLayer& shown = scene.layers[layer];
    const MeshContent content = makeMeshContent(scene, shown);
    const viewport::Frame frame = viewport::runWorldFrame(content.content);
    FaceReader reader(scene, frame, shown.rooms.size());
    for (usize draw = 0; draw < content.parts.size() && draw < frame.drawOutputs.size(); ++draw) {
        if (content.parts[draw] && frame.drawOutputs[draw]) {
            reader.readDraw(*frame.drawOutputs[draw], *content.parts[draw]);
        }
    }
    mesh.rooms = reader.takeRooms();
    for (RoomMesh& room : mesh.rooms) {
        groupVertices(room, kept != nullptr && kept->layer == layer ? findRoomMesh(*kept, room.room)
                                                                    : nullptr);
    }
    for (usize room = 0; room < reader.getLeftOut().size(); ++room) {
        if (const usize count = reader.getLeftOut()[room]; count > 0) {
            mesh.diagnostics.push_back(base::makeWarning(
                {}, std::format("room {} draws {} triangles from vertices outside any Vtx array of "
                                "the scene, which cannot be edited",
                                room, count)));
        }
    }
    return mesh;
}

const RoomMesh* findRoomMesh(const SceneMesh& mesh, const usize room) {
    const auto found = std::ranges::find(mesh.rooms, room, &RoomMesh::room);
    return found != mesh.rooms.end() ? &*found : nullptr;
}

glm::vec3 findFaceCenter(const RoomMesh& mesh, const MeshFace& face) {
    glm::vec3 sum(0.0F);
    for (const usize vertex : face.vertices) {
        sum += mesh.vertices[vertex].position;
    }
    return sum / 3.0F;
}

} // namespace toki::scene
