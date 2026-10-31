#include "SurfaceEdits.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionGeometry.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneSurface.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

using Point = std::array<s16, 3>;

[[nodiscard]] SurfacePolygon makeDraftPolygon(const RoomDraft& draft, const DraftFace& face,
                                              const std::optional<usize> read) {
    SurfacePolygon polygon{.material = *face.collision, .read = read};
    for (usize corner = 0; corner < 3; ++corner) {
        polygon.corners[corner] = roundToCollision(findCornerPosition(draft, face, corner));
    }
    return polygon;
}

/// The polygons a draft's solid faces are: in place of those they were
/// drafted from, where they were, and the rest after.
struct DraftedPolygons {
    std::map<usize, SurfacePolygon> replacing;
    std::vector<SurfacePolygon> added;

    void add(const RoomDraft& draft, const DraftFace& face, const std::optional<usize> read) {
        if (!face.collision) {
            return;
        }
        if (read && !replacing.contains(*read)) {
            replacing.emplace(*read, makeDraftPolygon(draft, face, read));
        } else {
            added.push_back(makeDraftPolygon(draft, face, std::nullopt));
        }
    }
};

/// Where each corner of each polygon goes, when it follows a move.
class CornerTargets {
public:
    CornerTargets(const Collision& collision, const SceneSurface& surface, const SceneMesh& mesh,
                  const std::span<const MeshVertexMove> meshMoves,
                  const std::span<const CollisionVertexMove> collisionMoves)
        : collision(collision), surface(surface), mesh(mesh) {
        for (const MeshVertexMove& move : meshMoves) {
            movedMesh.insert_or_assign({.room = move.room, .vertex = move.vertex},
                                       roundToCollision(move.position));
        }
        for (const CollisionVertexMove& move : collisionMoves) {
            movedCollision.insert_or_assign(move.vertex, move.position);
        }
    }

    [[nodiscard]] std::optional<Point> find(const usize polygon, const usize corner) const {
        const u16 vertex = getVertexIndex(collision.polygons[polygon], corner);
        if (vertex >= collision.vertices.size()) {
            return std::nullopt;
        }
        if (const auto found = movedCollision.find(vertex); found != movedCollision.end()) {
            return found->second;
        }
        if (polygon < surface.polygonFaces.size()) {
            if (const std::optional<MeshFaceRef>& face = surface.polygonFaces[polygon]) {
                return findFaceTarget(*face, collision.vertices[vertex]);
            }
        }
        if (vertex >= surface.welds.size()) {
            return std::nullopt;
        }
        for (const MeshVertexRef& weld : surface.welds[vertex]) {
            if (const auto found = movedMesh.find(weld); found != movedMesh.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] std::optional<Point> findFaceTarget(const MeshFaceRef& face,
                                                      const Point& standing) const {
        const RoomMesh* room = findRoomMesh(mesh, face.room);
        if (room == nullptr || face.face >= room->faces.size()) {
            return std::nullopt;
        }
        for (const usize vertex : room->faces[face.face].vertices) {
            if (roundToCollision(room->vertices[vertex].position) != standing) {
                continue;
            }
            const auto found = movedMesh.find({.room = face.room, .vertex = vertex});
            return found != movedMesh.end() ? std::optional<Point>(found->second) : std::nullopt;
        }
        return std::nullopt;
    }

    const Collision& collision;
    const SceneSurface& surface;
    const SceneMesh& mesh;
    std::map<MeshVertexRef, Point> movedMesh;
    std::map<usize, Point> movedCollision;
};

[[nodiscard]] std::vector<SurfacePolygon>
listDraftedPolygons(const Collision& collision, const SceneSurface& surface,
                    const std::span<const RoomDraft> drafts, const RoomDraft* solidOnly) {
    std::set<usize> rooms;
    DraftedPolygons drafted;
    for (const RoomDraft& draft : drafts) {
        rooms.insert(draft.room);
        for (const DraftFace& face : draft.faces) {
            drafted.add(draft, face,
                        face.original
                            ? findFacePolygon(surface, {.room = draft.room, .face = *face.original})
                            : std::nullopt);
        }
    }
    if (solidOnly != nullptr) {
        for (const DraftFace& face : solidOnly->faces) {
            const bool isSolidOnly = face.original &&
                                     *face.original < surface.polygonFaces.size() &&
                                     !surface.polygonFaces[*face.original];
            drafted.add(*solidOnly, face, isSolidOnly ? face.original : std::nullopt);
        }
    }
    const std::vector<SurfacePolygon> read = listSurfacePolygons(collision);
    std::vector<SurfacePolygon> polygons;
    for (usize polygon = 0; polygon < read.size(); ++polygon) {
        const std::optional<MeshFaceRef> face =
            polygon < surface.polygonFaces.size() ? surface.polygonFaces[polygon] : std::nullopt;
        const bool isDrafted = face ? rooms.contains(face->room) : solidOnly != nullptr;
        if (!isDrafted) {
            polygons.push_back(read[polygon]);
        } else if (const auto found = drafted.replacing.find(polygon);
                   found != drafted.replacing.end()) {
            polygons.push_back(found->second);
        }
    }
    polygons.insert(polygons.end(), drafted.added.begin(), drafted.added.end());
    return polygons;
}

} // namespace

SceneMesh makeSolidMesh(SceneMesh mesh, const SceneSurface& surface, const Collision& collision) {
    for (RoomMesh& room : mesh.rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            const std::optional<usize> polygon =
                findFacePolygon(surface, {.room = room.room, .face = face});
            room.faces[face].collision =
                polygon && *polygon < collision.polygons.size()
                    ? std::optional(getPolygonMaterial(collision.polygons[*polygon]))
                    : std::nullopt;
        }
    }
    return mesh;
}

RoomDraft makeSolidOnlyDraft(const Collision& collision, const SceneSurface& surface,
                             const usize room) {
    RoomDraft draft{.room = room};
    for (const Point& vertex : collision.vertices) {
        draft.vertices.push_back({.position = glm::vec3(vertex[0], vertex[1], vertex[2])});
    }
    for (const usize polygon : listSolidOnlyPolygons(surface)) {
        const CollisionPolygon& read = collision.polygons[polygon];
        DraftFace face{.collision = getPolygonMaterial(read), .original = polygon};
        bool isWhole = true;
        for (usize corner = 0; corner < 3; ++corner) {
            const u16 vertex = getVertexIndex(read, corner);
            isWhole = isWhole && vertex < collision.vertices.size();
            if (isWhole) {
                face.corners[corner].vertex = vertex;
                setVtxPosition(face.corners[corner].vtx, face.transform,
                               draft.vertices[vertex].position);
            }
        }
        if (isWhole) {
            draft.faces.push_back(face);
        }
    }
    return draft;
}

Collision makeDraftedCollision(const Collision& collision, const SceneSurface& surface,
                               const std::span<const RoomDraft> drafts,
                               const RoomDraft* solidOnly) {
    return makeSurfaceCollision(collision,
                                listDraftedPolygons(collision, surface, drafts, solidOnly));
}

usize countLeftOutPolygons(const Collision& collision, const SceneSurface& surface,
                           const std::span<const RoomDraft> drafts, const RoomDraft* solidOnly) {
    return static_cast<usize>(
        std::ranges::count_if(listDraftedPolygons(collision, surface, drafts, solidOnly),
                              [&collision](const SurfacePolygon& polygon) {
                                  return isPolygonLeftOut(collision, polygon);
                              }));
}

std::vector<usize> listFlatSolidFaces(const RoomDraft& draft) {
    std::vector<usize> flat;
    for (usize face = 0; face < draft.faces.size(); ++face) {
        const DraftFace& drafted = draft.faces[face];
        if (drafted.collision && !drafted.original &&
            isInLine(makeDraftPolygon(draft, drafted, std::nullopt).corners)) {
            flat.push_back(face);
        }
    }
    return flat;
}

std::vector<MeshVertexMove> findWeldedMeshMoves(const SceneSurface& surface,
                                                const std::span<const CollisionVertexMove> moves) {
    std::vector<MeshVertexMove> welded;
    for (const CollisionVertexMove& move : moves) {
        if (move.vertex >= surface.welds.size()) {
            continue;
        }
        for (const MeshVertexRef& weld : surface.welds[move.vertex]) {
            welded.push_back(
                {.room = weld.room,
                 .vertex = weld.vertex,
                 .position = glm::vec3(move.position[0], move.position[1], move.position[2])});
        }
    }
    return welded;
}

Collision makeWeldedCollision(const Collision& collision, const SceneSurface& surface,
                              const SceneMesh& mesh,
                              const std::span<const MeshVertexMove> meshMoves,
                              const std::span<const CollisionVertexMove> collisionMoves) {
    const CornerTargets targets(collision, surface, mesh, meshMoves, collisionMoves);
    std::vector<std::array<std::optional<Point>, 3>> corners(collision.polygons.size());
    std::map<usize, std::set<std::optional<Point>>> vertexTargets;
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        for (usize corner = 0; corner < 3; ++corner) {
            corners[polygon][corner] = targets.find(polygon, corner);
            const u16 vertex = getVertexIndex(collision.polygons[polygon], corner);
            if (vertex < collision.vertices.size()) {
                vertexTargets[vertex].insert(corners[polygon][corner]);
            }
        }
    }
    Collision moved = collision;
    std::set<usize> movedPolygons;
    std::map<Point, usize> splitVertices;
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        bool isMoved = false;
        for (usize corner = 0; corner < 3; ++corner) {
            const std::optional<Point>& target = corners[polygon][corner];
            const u16 vertex = getVertexIndex(collision.polygons[polygon], corner);
            if (!target || *target == collision.vertices[vertex]) {
                continue;
            }
            isMoved = true;
            if (vertexTargets[vertex].size() == 1) {
                moved.vertices[vertex] = *target;
                continue;
            }
            const auto [split, isNew] = splitVertices.try_emplace(*target, moved.vertices.size());
            if (isNew) {
                moved.vertices.push_back(*target);
            }
            CollisionPolygon& data = moved.polygons[polygon];
            data.vertexData[corner] =
                static_cast<u16>((data.vertexData[corner] & 0xE000U) | (split->second & 0x1FFFU));
        }
        if (isMoved) {
            movedPolygons.insert(polygon);
        }
    }
    for (const usize polygon : movedPolygons) {
        if (const std::optional<CollisionPolygon> faced =
                makeFacedPolygon(moved.polygons[polygon], moved.vertices)) {
            moved.polygons[polygon] = *faced;
        }
    }
    return makeBoundedCollision(std::move(moved));
}

} // namespace toki::scene
