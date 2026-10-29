#include "SurfaceOperations.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::optional<CollisionMaterial>
findMostCommon(const std::map<CollisionMaterial, usize>& counts) {
    const auto found = std::ranges::max_element(
        counts, {}, [](const std::pair<const CollisionMaterial, usize>& count) {
            return count.second;
        });
    return found != counts.end() ? std::optional(found->first) : std::nullopt;
}

} // namespace

CollisionMaterial findDefaultCollision(const SceneMesh& mesh, const Collision& collision,
                                       const MeshMaterial& material) {
    std::map<CollisionMaterial, usize> alike;
    for (const RoomMesh& room : mesh.rooms) {
        for (const MeshFace& face : room.faces) {
            if (face.collision && face.material == material) {
                ++alike[*face.collision];
            }
        }
    }
    if (const std::optional<CollisionMaterial> found = findMostCommon(alike)) {
        return *found;
    }
    std::map<CollisionMaterial, usize> all;
    for (const CollisionPolygon& polygon : collision.polygons) {
        ++all[getPolygonMaterial(polygon)];
    }
    return findMostCommon(all).value_or(CollisionMaterial{});
}

std::vector<OperatedRoom> setFacesCollision(const SceneMesh& mesh,
                                            const std::span<const MeshPick> picks,
                                            const std::optional<CollisionMaterial>& material) {
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr || pick.faces.empty()) {
            continue;
        }
        OperatedRoom made{.draft = makeRoomDraft(*room), .faces = pick.faces};
        for (const usize face : pick.faces) {
            if (face < made.draft.faces.size()) {
                made.draft.faces[face].collision = material;
            }
        }
        operated.push_back(std::move(made));
    }
    return operated;
}

std::vector<OperatedRoom> makeFacesSolid(const SceneMesh& mesh, const Collision& collision,
                                         const std::span<const MeshPick> picks) {
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr || pick.faces.empty()) {
            continue;
        }
        OperatedRoom made{.draft = makeRoomDraft(*room), .faces = pick.faces};
        for (const usize face : pick.faces) {
            if (face < made.draft.faces.size() && !made.draft.faces[face].collision) {
                made.draft.faces[face].collision =
                    findDefaultCollision(mesh, collision, room->faces[face].material);
            }
        }
        operated.push_back(std::move(made));
    }
    return operated;
}

OperatedRoom setSolidOnlyCollision(RoomDraft solidOnly, const std::span<const usize> faces,
                                   const std::optional<CollisionMaterial>& material) {
    if (!material) {
        deleteFaces(solidOnly, faces);
        return OperatedRoom{.draft = std::move(solidOnly)};
    }
    for (const usize face : faces) {
        if (face < solidOnly.faces.size()) {
            solidOnly.faces[face].collision = material;
        }
    }
    return OperatedRoom{.draft = std::move(solidOnly), .faces = {faces.begin(), faces.end()}};
}

std::vector<OperatedRoom> undrawFaces(const SceneMesh& mesh, const std::span<const MeshPick> picks,
                                      RoomDraft solidOnly) {
    std::vector<OperatedRoom> operated;
    OperatedRoom kept{.draft = std::move(solidOnly)};
    for (const MeshPick& pick : picks) {
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr) {
            continue;
        }
        OperatedRoom made{.draft = makeRoomDraft(*room)};
        std::vector<usize> undrawn;
        for (const usize face : pick.faces) {
            if (face >= made.draft.faces.size() || !made.draft.faces[face].collision) {
                continue;
            }
            const DraftFace& drawn = made.draft.faces[face];
            DraftFace solid{.collision = drawn.collision};
            for (usize corner = 0; corner < 3; ++corner) {
                solid.corners[corner].vertex =
                    addDraftVertex(kept.draft, findCornerPosition(made.draft, drawn, corner));
                setVtxPosition(solid.corners[corner].vtx, solid.transform,
                               kept.draft.vertices[solid.corners[corner].vertex].position);
            }
            kept.faces.push_back(kept.draft.faces.size());
            kept.draft.faces.push_back(solid);
            undrawn.push_back(face);
        }
        if (undrawn.empty()) {
            continue;
        }
        deleteFaces(made.draft, undrawn);
        operated.push_back(std::move(made));
    }
    if (!kept.faces.empty()) {
        operated.push_back(std::move(kept));
    }
    return operated;
}

} // namespace toki::scene
