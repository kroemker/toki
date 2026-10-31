#include "SurfaceOperations.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MaterialBindings.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneSurface.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

/// Whether face `face` of `room` stays as it is when made of `material`:
/// it is not solid, and cannot be, so making it solid would make nothing.
[[nodiscard]] bool isKeptUnsolid(const RoomMesh& room, const usize face,
                                 const std::optional<CollisionMaterial>& material) {
    return material && !room.faces[face].collision && !canFaceBeSolid(room, face);
}

[[nodiscard]] std::optional<CollisionMaterial>
findMostCommon(const std::map<CollisionMaterial, usize>& counts) {
    const auto found = std::ranges::max_element(
        counts, {},
        [](const std::pair<const CollisionMaterial, usize>& count) { return count.second; });
    return found != counts.end() ? std::optional(found->first) : std::nullopt;
}

[[nodiscard]] glm::vec3 findDraftCenter(const RoomDraft& draft, const DraftFace& face) {
    glm::vec3 sum(0.0F);
    for (usize corner = 0; corner < 3; ++corner) {
        sum += findCornerPosition(draft, face, corner);
    }
    return sum / 3.0F;
}

[[nodiscard]] usize findOrAddVertex(RoomDraft& draft, const glm::vec3& position) {
    const auto found = std::ranges::find(draft.vertices, position, &DraftVertex::position);
    return found != draft.vertices.end() ? static_cast<usize>(found - draft.vertices.begin())
                                         : addDraftVertex(draft, position);
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
            if (face < made.draft.faces.size() && !isKeptUnsolid(*room, face, material)) {
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
            if (face < made.draft.faces.size() && !made.draft.faces[face].collision &&
                canFaceBeSolid(*room, face)) {
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

std::vector<OperatedRoom> drawSolidOnlyFaces(const SceneMesh& mesh, RoomDraft solidOnly,
                                             const std::span<const usize> faces, const usize room) {
    const RoomMesh* drawing = findRoomMesh(mesh, room);
    if (drawing == nullptr || drawing->faces.empty()) {
        return {};
    }
    OperatedRoom made{.draft = makeRoomDraft(*drawing)};
    const usize drawnCount = made.draft.faces.size();
    std::vector<usize> taken;
    for (const usize face : faces) {
        if (face >= solidOnly.faces.size()) {
            continue;
        }
        const DraftFace& solid = solidOnly.faces[face];
        const glm::vec3 center = findDraftCenter(solidOnly, solid);
        usize like = 0;
        f32 nearest = std::numeric_limits<f32>::max();
        for (usize drawn = 0; drawn < drawnCount; ++drawn) {
            const glm::vec3 offset = findDraftCenter(made.draft, made.draft.faces[drawn]) - center;
            if (const f32 distance = glm::dot(offset, offset); distance < nearest) {
                nearest = distance;
                like = drawn;
            }
        }
        const DraftFace source = made.draft.faces[like];
        DraftFace drawnFace = source;
        drawnFace.original.reset();
        drawnFace.collision = solid.collision;
        for (usize corner = 0; corner < 3; ++corner) {
            const glm::vec3 position = findCornerPosition(solidOnly, solid, corner);
            std::array<u8, 16> vtx = makeProjectedVtx(made.draft, source, position);
            setVtxPosition(vtx, source.transform, position);
            drawnFace.corners[corner] =
                DraftCorner{.vertex = findOrAddVertex(made.draft, position), .vtx = vtx};
        }
        made.faces.push_back(made.draft.faces.size());
        made.draft.faces.push_back(drawnFace);
        taken.push_back(face);
    }
    if (taken.empty()) {
        return {};
    }
    deleteFaces(solidOnly, taken);
    std::vector<OperatedRoom> operated;
    operated.push_back(std::move(made));
    operated.push_back(OperatedRoom{.draft = std::move(solidOnly)});
    return operated;
}

std::vector<OperatedRoom> makeBoundFaces(std::vector<OperatedRoom> operated,
                                         const std::optional<ResolvedBinding>& binding) {
    if (!binding || !binding->collision) {
        return operated;
    }
    for (OperatedRoom& room : operated) {
        for (const usize face : room.faces) {
            if (face < room.draft.faces.size()) {
                room.draft.faces[face].collision = *binding->collision;
            }
        }
    }
    return operated;
}

std::vector<std::optional<ResolvedBinding>>
resolveMaterialBindings(const MaterialBindings& bindings, const Collision& collision,
                        const SceneMesh& mesh, const std::vector<SceneMaterial>& materials) {
    std::vector<std::optional<ResolvedBinding>> resolved(materials.size());
    for (usize material = 0; material < materials.size(); ++material) {
        const std::optional<MaterialBinding> binding =
            findMaterialBinding(bindings, materials[material]);
        if (!binding) {
            continue;
        }
        ResolvedBinding made{.binding = *binding,
                             .collision = findBoundCollision(collision, *binding)};
        if (made.collision) {
            made.unbound = countUnbound(mesh, materials, material, *made.collision);
        }
        resolved[material] = std::move(made);
    }
    return resolved;
}

bool isCollisionMixed(const SceneMesh& mesh, const std::vector<SceneMaterial>& materials,
                      const usize material) {
    if (material >= materials.size()) {
        return false;
    }
    std::optional<std::optional<CollisionMaterial>> first;
    for (const RoomMesh& room : mesh.rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            const MeshFace& drawn = room.faces[face];
            if (!isDrawnUnder(materials[material], drawn) ||
                (!drawn.collision && !canFaceBeSolid(room, face))) {
                continue;
            }
            if (!first) {
                first = drawn.collision;
            } else if (*first != drawn.collision) {
                return true;
            }
        }
    }
    return false;
}

usize countUnbound(const SceneMesh& mesh, const std::vector<SceneMaterial>& materials,
                   const usize material, const std::optional<CollisionMaterial>& bound) {
    usize count = 0;
    if (material >= materials.size()) {
        return count;
    }
    for (const RoomMesh& room : mesh.rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (room.faces[face].collision != bound &&
                isDrawnUnder(materials[material], room.faces[face]) &&
                !isKeptUnsolid(room, face, bound)) {
                ++count;
            }
        }
    }
    return count;
}

std::vector<OperatedRoom> bindFaces(const SceneMesh& mesh,
                                    const std::vector<SceneMaterial>& materials,
                                    const usize material,
                                    const std::optional<CollisionMaterial>& bound) {
    std::vector<OperatedRoom> operated;
    if (material >= materials.size()) {
        return operated;
    }
    for (const RoomMesh& room : mesh.rooms) {
        std::vector<usize> faces;
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (room.faces[face].collision != bound &&
                isDrawnUnder(materials[material], room.faces[face]) &&
                !isKeptUnsolid(room, face, bound)) {
                faces.push_back(face);
            }
        }
        if (faces.empty()) {
            continue;
        }
        OperatedRoom made{.draft = makeRoomDraft(room)};
        for (const usize face : faces) {
            made.draft.faces[face].collision = bound;
        }
        operated.push_back(std::move(made));
    }
    return operated;
}

} // namespace toki::scene
