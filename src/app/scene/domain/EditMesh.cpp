#include "EditMesh.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneSurface.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace toki::scene {

namespace {

constexpr f32 ParallelLimit = 1.0e-7F;

/// How far along `direction` from `origin` the ray meets the triangle, if it
/// does, as Möller and Trumbore find it.
[[nodiscard]] std::optional<f32> findHitDistance(const glm::vec3& origin,
                                                 const glm::vec3& direction, const glm::vec3& a,
                                                 const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 across = glm::cross(direction, ac);
    const f32 determinant = glm::dot(ab, across);
    if (std::abs(determinant) < ParallelLimit) {
        return std::nullopt;
    }
    const f32 inverse = 1.0F / determinant;
    const glm::vec3 fromA = origin - a;
    const f32 u = glm::dot(fromA, across) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }
    const glm::vec3 up = glm::cross(fromA, ab);
    const f32 v = glm::dot(direction, up) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return std::nullopt;
    }
    const f32 distance = glm::dot(ac, up) * inverse;
    if (distance <= 0.0F) {
        return std::nullopt;
    }
    return distance;
}

[[nodiscard]] std::vector<MeshEdge> makeEdges(const std::vector<std::array<usize, 3>>& faces) {
    std::set<MeshEdge> edges;
    for (const std::array<usize, 3>& face : faces) {
        for (usize corner = 0; corner < 3; ++corner) {
            const usize from = face[corner];
            const usize to = face[(corner + 1) % 3];
            if (from != to) {
                edges.insert(MeshEdge{.vertices = {std::min(from, to), std::max(from, to)}});
            }
        }
    }
    return {edges.begin(), edges.end()};
}

} // namespace

EditMesh makeMeshEdit(const SceneMesh& mesh, const usize roomCount) {
    EditMesh edit{.pieces = std::vector<EditPiece>(roomCount)};
    for (const RoomMesh& room : mesh.rooms) {
        if (room.room >= roomCount) {
            continue;
        }
        EditPiece& piece = edit.pieces[room.room];
        for (const MeshVertex& vertex : room.vertices) {
            piece.vertices.push_back(vertex.position);
        }
        for (const MeshFace& face : room.faces) {
            piece.faces.push_back(face.vertices);
        }
        piece.edges = room.edges;
    }
    return edit;
}

EditMesh makeCollisionEdit(const Collision& collision) {
    EditPiece piece;
    for (const std::array<s16, 3>& vertex : collision.vertices) {
        piece.vertices.emplace_back(vertex[0], vertex[1], vertex[2]);
    }
    for (const CollisionPolygon& polygon : collision.polygons) {
        std::array<usize, 3> face{};
        bool isWhole = true;
        for (usize corner = 0; corner < 3; ++corner) {
            face[corner] = getVertexIndex(polygon, corner);
            isWhole &= face[corner] < piece.vertices.size();
        }
        if (isWhole) {
            piece.faces.push_back(face);
        }
    }
    piece.edges = makeEdges(piece.faces);
    return EditMesh{.pieces = {std::move(piece)}};
}

EditMesh makeSurfaceEdit(const SceneMesh& mesh, const usize roomCount, const SceneSurface& surface,
                         const Collision& collision) {
    EditMesh edit = makeMeshEdit(mesh, roomCount);
    Collision solidOnly = collision;
    solidOnly.polygons.clear();
    std::vector<usize> sources;
    for (const usize polygon : listSolidOnlyPolygons(surface)) {
        const CollisionPolygon& read = collision.polygons[polygon];
        if (std::ranges::all_of(std::array<usize, 3>{0, 1, 2}, [&](const usize corner) {
                return getVertexIndex(read, corner) < collision.vertices.size();
            })) {
            solidOnly.polygons.push_back(read);
            sources.push_back(polygon);
        }
    }
    EditPiece piece = std::move(makeCollisionEdit(solidOnly).pieces.front());
    piece.faceSources = std::move(sources);
    edit.pieces.push_back(std::move(piece));
    return edit;
}

std::vector<bool> findUsedVertices(const EditPiece& piece) {
    std::vector<bool> isUsed(piece.vertices.size(), false);
    for (const std::array<usize, 3>& face : piece.faces) {
        for (const usize vertex : face) {
            if (vertex < isUsed.size()) {
                isUsed[vertex] = true;
            }
        }
    }
    return isUsed;
}

usize findFaceSource(const EditPiece& piece, const usize face) {
    return face < piece.faceSources.size() ? piece.faceSources[face] : face;
}

std::optional<ComponentRef> findHitFace(const EditMesh& mesh, const glm::vec3& origin,
                                        const glm::vec3& direction,
                                        const std::vector<bool>& isShown) {
    std::optional<ComponentRef> hit;
    f32 nearest = std::numeric_limits<f32>::max();
    for (usize piece = 0; piece < mesh.pieces.size(); ++piece) {
        if (piece < isShown.size() && !isShown[piece]) {
            continue;
        }
        const EditPiece& faces = mesh.pieces[piece];
        for (usize face = 0; face < faces.faces.size(); ++face) {
            const std::array<usize, 3>& corners = faces.faces[face];
            const std::optional<f32> distance =
                findHitDistance(origin, direction, faces.vertices[corners[0]],
                                faces.vertices[corners[1]], faces.vertices[corners[2]]);
            if (distance && *distance < nearest) {
                nearest = *distance;
                hit = ComponentRef{.piece = piece, .index = face};
            }
        }
    }
    return hit;
}

glm::vec3 findFaceMiddle(const EditPiece& piece, const usize face) {
    const std::array<usize, 3>& corners = piece.faces[face];
    return (piece.vertices[corners[0]] + piece.vertices[corners[1]] + piece.vertices[corners[2]]) /
           3.0F;
}

} // namespace toki::scene
