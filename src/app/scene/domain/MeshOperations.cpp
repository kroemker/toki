#include "MeshOperations.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/RoomMesh.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] bool hasTwoCornersAtOne(const DraftFace& face) {
    return face.corners[0].vertex == face.corners[1].vertex ||
           face.corners[1].vertex == face.corners[2].vertex ||
           face.corners[0].vertex == face.corners[2].vertex;
}

} // namespace

void deleteFaces(RoomDraft& draft, const std::span<const usize> faces) {
    const std::set<usize> deleted(faces.begin(), faces.end());
    std::vector<DraftFace> kept;
    for (usize face = 0; face < draft.faces.size(); ++face) {
        if (!deleted.contains(face)) {
            kept.push_back(std::move(draft.faces[face]));
        }
    }
    draft.faces = std::move(kept);
}

void flipFaces(RoomDraft& draft, const std::span<const usize> faces) {
    for (const usize face : faces) {
        if (face < draft.faces.size()) {
            std::swap(draft.faces[face].corners[1], draft.faces[face].corners[2]);
        }
    }
}

std::optional<usize> mergeVertices(RoomDraft& draft, const std::span<const usize> vertices) {
    const std::set<usize> merged(vertices.begin(), vertices.end());
    if (merged.size() < 2) {
        return merged.empty() ? std::nullopt : std::optional(*merged.begin());
    }
    glm::vec3 sum(0.0F);
    for (const usize vertex : merged) {
        sum += draft.vertices[vertex].position;
    }
    const usize target = *merged.begin();
    draft.vertices[target] = DraftVertex{.position = sum / static_cast<f32>(merged.size()),
                                         .isMoved = true};
    for (DraftFace& face : draft.faces) {
        for (DraftCorner& corner : face.corners) {
            if (merged.contains(corner.vertex)) {
                corner.vertex = target;
            }
        }
    }
    std::erase_if(draft.faces, hasTwoCornersAtOne);
    return target;
}

void splitFaces(RoomDraft& draft, const std::span<const usize> faces) {
    const std::set<usize> split(faces.begin(), faces.end());
    std::set<usize> outside;
    for (usize face = 0; face < draft.faces.size(); ++face) {
        if (!split.contains(face)) {
            for (const DraftCorner& corner : draft.faces[face].corners) {
                outside.insert(corner.vertex);
            }
        }
    }
    std::map<usize, usize> copies;
    for (const usize face : split) {
        if (face >= draft.faces.size()) {
            continue;
        }
        for (DraftCorner& corner : draft.faces[face].corners) {
            if (!outside.contains(corner.vertex)) {
                continue;
            }
            const auto [copy, isNew] = copies.emplace(corner.vertex, draft.vertices.size());
            if (isNew) {
                DraftVertex vertex = draft.vertices[corner.vertex];
                draft.vertices.push_back(vertex);
            }
            corner.vertex = copy->second;
        }
    }
}

std::vector<OperatedRoom> operateMesh(const SceneMesh& mesh, const MeshOperation operation,
                                      const std::span<const MeshPick> picks) {
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr) {
            continue;
        }
        OperatedRoom made{.draft = makeRoomDraft(*room)};
        switch (operation) {
        case MeshOperation::Delete:
            if (pick.touchedFaces.empty()) {
                continue;
            }
            deleteFaces(made.draft, pick.touchedFaces);
            break;
        case MeshOperation::Flip:
            if (pick.faces.empty()) {
                continue;
            }
            flipFaces(made.draft, pick.faces);
            made.faces = pick.faces;
            break;
        case MeshOperation::Merge:
            if (pick.vertices.size() < 2) {
                continue;
            }
            if (const std::optional<usize> merged = mergeVertices(made.draft, pick.vertices)) {
                made.vertices.push_back(*merged);
            }
            break;
        case MeshOperation::Split:
            if (pick.faces.empty()) {
                continue;
            }
            splitFaces(made.draft, pick.faces);
            made.faces = pick.faces;
            break;
        }
        operated.push_back(std::move(made));
    }
    return operated;
}

} // namespace toki::scene
