#include "MeshShapes.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"
#include "n64/BigEndian.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize TextureOffset = 8;
constexpr f32 TexelScale = 32.0F;

/// A quad of a shape: its corners, counterclockwise seen from outside.
using Quad = std::array<glm::vec3, 4>;

[[nodiscard]] std::vector<Quad> makeQuads(const MeshShape shape, const glm::vec3& low,
                                          const glm::vec3& high) {
    const auto at = [&low, &high](const bool x, const bool y, const bool z) {
        return glm::vec3(x ? high.x : low.x, y ? high.y : low.y, z ? high.z : low.z);
    };
    if (shape == MeshShape::Plane) {
        return {{at(false, false, false), at(false, false, true), at(true, false, true),
                 at(true, false, false)}};
    }
    return {
        {at(false, true, false), at(false, true, true), at(true, true, true),
         at(true, true, false)},
        {at(false, false, false), at(true, false, false), at(true, false, true),
         at(false, false, true)},
        {at(false, false, true), at(true, false, true), at(true, true, true),
         at(false, true, true)},
        {at(true, false, false), at(false, false, false), at(false, true, false),
         at(true, true, false)},
        {at(true, false, true), at(true, false, false), at(true, true, false),
         at(true, true, true)},
        {at(false, false, false), at(false, false, true), at(false, true, true),
         at(false, true, false)},
    };
}

[[nodiscard]] s16 makeTexel(const f32 units) {
    return static_cast<s16>(
        std::clamp(std::lround(units * TexelScale), long{INT16_MIN}, long{INT16_MAX}));
}

/// `like`'s `Vtx` with texture coordinates read off the quad's plane at
/// `point`, measured from `origin`.
[[nodiscard]] std::array<u8, 16> makeShapeVtx(const DraftFace& like, const Quad& quad,
                                              const glm::vec3& point, const glm::vec3& origin) {
    const glm::vec3 across = glm::normalize(quad[1] - quad[0]);
    const glm::vec3 down = glm::normalize(quad[3] - quad[0]);
    std::array<u8, 16> vtx = like.corners[0].vtx;
    n64::writeU16(vtx, TextureOffset,
                  static_cast<u16>(makeTexel(glm::dot(point - origin, across))));
    n64::writeU16(vtx, TextureOffset + 2,
                  static_cast<u16>(makeTexel(glm::dot(point - origin, down))));
    return vtx;
}

} // namespace

std::vector<usize> addMeshShape(RoomDraft& draft, const MeshShape shape, const glm::vec3& center,
                                const f32 size, const DraftFace& like) {
    const f32 half = size / 2.0F;
    const glm::vec3 low = center - glm::vec3(half, 0.0F, half);
    const glm::vec3 high = center + glm::vec3(half, shape == MeshShape::Box ? size : 0.0F, half);
    std::map<std::array<f32, 3>, usize> vertices;
    const auto findVertex = [&draft, &vertices](const glm::vec3& position) {
        const auto [found, isNew] =
            vertices.emplace(std::array{position.x, position.y, position.z}, draft.vertices.size());
        if (isNew) {
            static_cast<void>(addDraftVertex(draft, position));
        }
        return found->second;
    };
    std::vector<usize> added;
    for (const Quad& quad : makeQuads(shape, low, high)) {
        for (const std::array<usize, 3>& corners :
             {std::array<usize, 3>{0, 1, 2}, std::array<usize, 3>{0, 2, 3}}) {
            DraftFace face = like;
            face.original.reset();
            for (usize corner = 0; corner < 3; ++corner) {
                const glm::vec3& point = quad[corners[corner]];
                face.corners[corner] = DraftCorner{.vertex = findVertex(point),
                                                   .vtx = makeShapeVtx(like, quad, point, low)};
            }
            added.push_back(draft.faces.size());
            draft.faces.push_back(face);
        }
    }
    return added;
}

std::expected<std::vector<OperatedRoom>, base::Diagnostic>
moveFacesToRoom(const SceneMesh& mesh, const std::span<const MeshPick> picks, const usize room,
                const std::vector<SceneMaterial>& materials) {
    const RoomMesh* target = findRoomMesh(mesh, room);
    if (target == nullptr) {
        return std::unexpected(base::makeError({}, std::format("the mesh has no room {}", room)));
    }
    OperatedRoom into{.draft = makeRoomDraft(*target)};
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        const RoomMesh* source = findRoomMesh(mesh, pick.room);
        if (pick.room == room || source == nullptr || pick.faces.empty()) {
            continue;
        }
        OperatedRoom from{.draft = makeRoomDraft(*source)};
        std::map<usize, usize> copies;
        for (const usize face : pick.faces) {
            const std::optional<usize> material = findFaceMaterial(materials, source->faces[face]);
            const MaterialRoom* drawn = material ? materials[*material].findRoom(room) : nullptr;
            if (drawn == nullptr || !drawn->sample) {
                return std::unexpected(base::makeError(
                    {}, std::format("room {} draws nothing with '{}', the material of a face "
                                    "moved there",
                                    room, material ? materials[*material].name : "?")));
            }
            const auto like = std::ranges::find(into.draft.faces, *drawn->sample, &DraftFace::draw);
            const DraftFace& moved = from.draft.faces[face];
            DraftFace made = like != into.draft.faces.end() ? *like : moved;
            made.draw = *drawn->sample;
            made.original.reset();
            for (usize corner = 0; corner < 3; ++corner) {
                const usize vertex = moved.corners[corner].vertex;
                const auto [copy, isNew] = copies.emplace(vertex, into.draft.vertices.size());
                if (isNew) {
                    static_cast<void>(
                        addDraftVertex(into.draft, findCornerPosition(from.draft, moved, corner)));
                }
                made.corners[corner] =
                    DraftCorner{.vertex = copy->second, .vtx = moved.corners[corner].vtx};
            }
            into.faces.push_back(into.draft.faces.size());
            into.draft.faces.push_back(made);
        }
        deleteFaces(from.draft, pick.faces);
        operated.push_back(std::move(from));
    }
    if (into.faces.empty()) {
        return std::vector<OperatedRoom>{};
    }
    operated.push_back(std::move(into));
    return operated;
}

} // namespace toki::scene
