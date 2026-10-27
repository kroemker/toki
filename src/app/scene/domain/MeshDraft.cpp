#include "MeshDraft.h"

#include "app/scene/domain/RoomMesh.h"
#include "n64/BigEndian.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize TextureOffset = 8;
constexpr usize ColorOffset = 12;
constexpr f32 Flat = 1.0e-6F;

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

[[nodiscard]] std::array<s16, 3> readStored(const std::array<u8, 16>& vtx) {
    return {n64::readS16(vtx, 0), n64::readS16(vtx, 2), n64::readS16(vtx, 4)};
}

/// The weights of `face`'s corners that make `point` projected onto its plane.
[[nodiscard]] std::array<f32, 3> findWeights(const std::array<glm::vec3, 3>& corners,
                                             const glm::vec3& point) {
    const glm::vec3 first = corners[1] - corners[0];
    const glm::vec3 second = corners[2] - corners[0];
    const glm::vec3 offset = point - corners[0];
    const f32 firstFirst = glm::dot(first, first);
    const f32 firstSecond = glm::dot(first, second);
    const f32 secondSecond = glm::dot(second, second);
    const f32 denominator = (firstFirst * secondSecond) - (firstSecond * firstSecond);
    if (std::abs(denominator) < Flat) {
        return {1.0F, 0.0F, 0.0F};
    }
    const f32 offsetFirst = glm::dot(offset, first);
    const f32 offsetSecond = glm::dot(offset, second);
    const f32 along = ((secondSecond * offsetFirst) - (firstSecond * offsetSecond)) / denominator;
    const f32 across = ((firstFirst * offsetSecond) - (firstSecond * offsetFirst)) / denominator;
    return {1.0F - along - across, along, across};
}

} // namespace

RoomDraft makeRoomDraft(const RoomMesh& mesh) {
    RoomDraft draft{.room = mesh.room};
    for (const MeshVertex& vertex : mesh.vertices) {
        draft.vertices.push_back(DraftVertex{.position = vertex.position});
    }
    for (usize index = 0; index < mesh.faces.size(); ++index) {
        const MeshFace& face = mesh.faces[index];
        DraftFace drafted{.draw = face.draw,
                          .transform = mesh.entries[face.corners[0]].transform,
                          .isLit = face.isLit,
                          .isTranslucent = face.isTranslucent,
                          .part = face.part,
                          .original = index};
        for (usize corner = 0; corner < 3; ++corner) {
            drafted.corners[corner] = DraftCorner{.vertex = face.vertices[corner],
                                                  .vtx = mesh.entries[face.corners[corner]].vtx};
        }
        draft.faces.push_back(std::move(drafted));
    }
    return draft;
}

glm::vec3 findCornerPosition(const RoomDraft& draft, const DraftFace& face, const usize corner) {
    const DraftVertex& vertex = draft.vertices[face.corners[corner].vertex];
    if (vertex.isMoved) {
        return vertex.position;
    }
    const std::array<s16, 3> stored = readStored(face.corners[corner].vtx);
    return glm::vec3(face.transform * glm::vec4(static_cast<f32>(stored[0]),
                                                static_cast<f32>(stored[1]),
                                                static_cast<f32>(stored[2]), 1.0F));
}

void setVtxPosition(std::array<u8, 16>& vtx, const glm::mat4& transform, const glm::vec3& world) {
    const glm::vec4 local = glm::inverse(transform) * glm::vec4(world, 1.0F);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        n64::writeU16(vtx, static_cast<usize>(axis) * 2,
                      static_cast<u16>(roundToS16(local[axis])));
    }
}

std::array<u8, 16> makeCornerVtx(const RoomDraft& draft, const DraftFace& face,
                                 const usize corner) {
    std::array<u8, 16> vtx = face.corners[corner].vtx;
    const DraftVertex& vertex = draft.vertices[face.corners[corner].vertex];
    if (vertex.isMoved) {
        setVtxPosition(vtx, face.transform, vertex.position);
    }
    return vtx;
}

std::array<u8, 16> makeProjectedVtx(const RoomDraft& draft, const DraftFace& face,
                                    const glm::vec3& point) {
    const std::array<glm::vec3, 3> corners{findCornerPosition(draft, face, 0),
                                           findCornerPosition(draft, face, 1),
                                           findCornerPosition(draft, face, 2)};
    const std::array<f32, 3> weights = findWeights(corners, point);
    std::array<u8, 16> vtx = face.corners[0].vtx;
    for (usize axis = 0; axis < 2; ++axis) {
        f32 texture = 0.0F;
        for (usize corner = 0; corner < 3; ++corner) {
            texture += weights[corner] * static_cast<f32>(n64::readS16(
                                             face.corners[corner].vtx, TextureOffset + (axis * 2)));
        }
        n64::writeU16(vtx, TextureOffset + (axis * 2), static_cast<u16>(roundToS16(texture)));
    }
    for (usize channel = 0; channel < 4; ++channel) {
        f32 value = 0.0F;
        for (usize corner = 0; corner < 3; ++corner) {
            const u8 stored = face.corners[corner].vtx[ColorOffset + channel];
            value += weights[corner] * (face.isLit && channel < 3
                                            ? static_cast<f32>(static_cast<s8>(stored))
                                            : static_cast<f32>(stored));
        }
        vtx[ColorOffset + channel] =
            face.isLit && channel < 3
                ? static_cast<u8>(static_cast<s8>(std::clamp(std::lround(value), -128L, 127L)))
                : static_cast<u8>(std::clamp(std::lround(value), 0L, 255L));
    }
    setVtxPosition(vtx, face.transform, point);
    return vtx;
}

std::vector<bool> findUsedVertices(const RoomDraft& draft) {
    std::vector<bool> used(draft.vertices.size(), false);
    for (const DraftFace& face : draft.faces) {
        for (const DraftCorner& corner : face.corners) {
            used[corner.vertex] = true;
        }
    }
    return used;
}

usize addDraftVertex(RoomDraft& draft, const glm::vec3& position) {
    draft.vertices.push_back(DraftVertex{.position = position, .isMoved = true});
    return draft.vertices.size() - 1;
}

} // namespace toki::scene
