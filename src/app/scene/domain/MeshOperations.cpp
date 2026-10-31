#include "MeshOperations.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/RoomMesh.h"

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

[[nodiscard]] bool hasTwoCornersAtOne(const DraftFace& face) {
    return face.corners[0].vertex == face.corners[1].vertex ||
           face.corners[1].vertex == face.corners[2].vertex ||
           face.corners[0].vertex == face.corners[2].vertex;
}

[[nodiscard]] DraftFace makeGrownFace(const RoomDraft& draft, const DraftFace& source,
                                      const std::array<usize, 3>& vertices) {
    DraftFace grown = source;
    grown.original.reset();
    for (usize corner = 0; corner < 3; ++corner) {
        const glm::vec3& position = draft.vertices[vertices[corner]].position;
        grown.corners[corner] = DraftCorner{.vertex = vertices[corner],
                                            .vtx = makeProjectedVtx(draft, source, position)};
    }
    return grown;
}

/// The faces one face is made into, the first `picked` of them to pick.
struct Grown {
    std::vector<DraftFace> faces;
    usize picked = 0;
};

/// Replaces each face with what `grow` makes of it, in its place, and
/// returns where the faces to pick ended up.
template <typename Grow>
std::vector<usize> growFaces(RoomDraft& draft, Grow grow) {
    std::vector<DraftFace> faces;
    std::vector<usize> made;
    const std::vector<DraftFace> before = draft.faces;
    for (usize face = 0; face < before.size(); ++face) {
        const Grown grown = grow(face, before[face]);
        for (usize i = 0; i < grown.faces.size(); ++i) {
            if (i < grown.picked) {
                made.push_back(faces.size());
            }
            faces.push_back(grown.faces[i]);
        }
    }
    draft.faces = std::move(faces);
    return made;
}

using EdgeKey = std::pair<usize, usize>;

[[nodiscard]] EdgeKey makeEdgeKey(const usize first, const usize second) {
    return {std::min(first, second), std::max(first, second)};
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
    draft.vertices[target] =
        DraftVertex{.position = sum / static_cast<f32>(merged.size()), .isMoved = true};
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

std::vector<usize> extrudeFaces(RoomDraft& draft, const std::span<const usize> faces) {
    const std::set<usize> region(faces.begin(), faces.end());
    std::map<EdgeKey, usize> uses;
    for (const usize face : region) {
        if (face >= draft.faces.size()) {
            continue;
        }
        for (usize corner = 0; corner < 3; ++corner) {
            ++uses[makeEdgeKey(draft.faces[face].corners[corner].vertex,
                               draft.faces[face].corners[(corner + 1) % 3].vertex)];
        }
    }
    std::map<usize, usize> copies;
    const auto copyOf = [&draft, &copies](const usize vertex) {
        const auto [copy, isNew] = copies.emplace(vertex, draft.vertices.size());
        if (isNew) {
            draft.vertices.push_back(DraftVertex{.position = draft.vertices[vertex].position});
        }
        return copy->second;
    };
    std::vector<usize> capped;
    const std::vector<DraftFace> before = draft.faces;
    std::vector<DraftFace> made;
    for (usize face = 0; face < before.size(); ++face) {
        if (!region.contains(face)) {
            made.push_back(before[face]);
            continue;
        }
        const DraftFace& source = before[face];
        DraftFace cap = source;
        for (DraftCorner& corner : cap.corners) {
            corner.vertex = copyOf(corner.vertex);
        }
        capped.push_back(made.size());
        made.push_back(cap);
        for (usize corner = 0; corner < 3; ++corner) {
            const usize from = source.corners[corner].vertex;
            const usize to = source.corners[(corner + 1) % 3].vertex;
            if (uses[makeEdgeKey(from, to)] != 1) {
                continue;
            }
            const usize fromCopy = copyOf(from);
            const usize toCopy = copyOf(to);
            DraftFace first = source;
            first.original.reset();
            first.corners = {
                source.corners[corner], source.corners[(corner + 1) % 3],
                DraftCorner{.vertex = toCopy, .vtx = source.corners[(corner + 1) % 3].vtx}};
            DraftFace second = first;
            second.corners = {
                source.corners[corner],
                DraftCorner{.vertex = toCopy, .vtx = source.corners[(corner + 1) % 3].vtx},
                DraftCorner{.vertex = fromCopy, .vtx = source.corners[corner].vtx}};
            made.push_back(first);
            made.push_back(second);
        }
    }
    draft.faces = std::move(made);
    return capped;
}

std::vector<usize> insetFaces(RoomDraft& draft, const std::span<const usize> faces) {
    const std::set<usize> picked(faces.begin(), faces.end());
    return growFaces(draft, [&](const usize index, const DraftFace& face) {
        if (!picked.contains(index)) {
            return Grown{.faces = {face}};
        }
        glm::vec3 middle(0.0F);
        for (usize corner = 0; corner < 3; ++corner) {
            middle += findCornerPosition(draft, face, corner);
        }
        middle /= 3.0F;
        std::array<usize, 3> inner{};
        for (usize corner = 0; corner < 3; ++corner) {
            const glm::vec3 outer = findCornerPosition(draft, face, corner);
            inner[corner] = addDraftVertex(draft, outer + ((middle - outer) * InsetFraction));
        }
        std::vector<DraftFace> grown{makeGrownFace(draft, face, inner)};
        for (usize corner = 0; corner < 3; ++corner) {
            const usize next = (corner + 1) % 3;
            DraftFace first = makeGrownFace(
                draft, face, {face.corners[corner].vertex, face.corners[next].vertex, inner[next]});
            first.corners[0] = face.corners[corner];
            first.corners[1] = face.corners[next];
            DraftFace second = makeGrownFace(
                draft, face, {face.corners[corner].vertex, inner[next], inner[corner]});
            second.corners[0] = face.corners[corner];
            grown.push_back(first);
            grown.push_back(second);
        }
        return Grown{.faces = std::move(grown), .picked = 1};
    });
}

std::vector<usize> subdivideFaces(RoomDraft& draft, const std::span<const usize> faces) {
    const std::set<usize> picked(faces.begin(), faces.end());
    std::map<EdgeKey, usize> middles;
    for (const usize face : picked) {
        if (face >= draft.faces.size()) {
            continue;
        }
        for (usize corner = 0; corner < 3; ++corner) {
            const usize from = draft.faces[face].corners[corner].vertex;
            const usize to = draft.faces[face].corners[(corner + 1) % 3].vertex;
            const EdgeKey key = makeEdgeKey(from, to);
            if (!middles.contains(key)) {
                const glm::vec3 middle =
                    (draft.vertices[from].position + draft.vertices[to].position) / 2.0F;
                middles.emplace(key, addDraftVertex(draft, middle));
            }
        }
    }
    return growFaces(draft, [&](const usize index, const DraftFace& face) {
        std::array<std::optional<usize>, 3> cuts{};
        usize count = 0;
        for (usize corner = 0; corner < 3; ++corner) {
            const auto found = middles.find(
                makeEdgeKey(face.corners[corner].vertex, face.corners[(corner + 1) % 3].vertex));
            if (found != middles.end()) {
                cuts[corner] = found->second;
                ++count;
            }
        }
        if (count == 0) {
            return Grown{.faces = {face}};
        }
        usize first = 0;
        while (count < 3 && (!cuts[first] || (count == 2 && !cuts[(first + 1) % 3]))) {
            ++first;
        }
        const usize a = face.corners[first].vertex;
        const usize b = face.corners[(first + 1) % 3].vertex;
        const usize c = face.corners[(first + 2) % 3].vertex;
        const auto cut = [&cuts, first](const usize edge) { return *cuts[(first + edge) % 3]; };
        std::vector<std::array<usize, 3>> pieces;
        if (count == 1) {
            pieces = {{a, cut(0), c}, {cut(0), b, c}};
        } else if (count == 2) {
            pieces = {{cut(0), b, cut(1)}, {a, cut(0), cut(1)}, {a, cut(1), c}};
        } else {
            pieces = {{a, cut(0), cut(2)},
                      {cut(0), b, cut(1)},
                      {cut(2), cut(1), c},
                      {cut(0), cut(1), cut(2)}};
        }
        std::vector<DraftFace> grown;
        for (const std::array<usize, 3>& piece : pieces) {
            DraftFace made = makeGrownFace(draft, face, piece);
            for (usize corner = 0; corner < 3; ++corner) {
                for (const DraftCorner& kept : face.corners) {
                    if (kept.vertex == piece[corner]) {
                        made.corners[corner] = kept;
                    }
                }
            }
            grown.push_back(made);
        }
        const usize pickedCount = picked.contains(index) ? grown.size() : 0;
        return Grown{.faces = std::move(grown), .picked = pickedCount};
    });
}

std::optional<OperatedRoom> operateDraft(RoomDraft draft, const MeshOperation operation,
                                         const MeshPick& pick) {
    OperatedRoom made{.draft = std::move(draft)};
    switch (operation) {
    case MeshOperation::Delete:
        if (pick.touchedFaces.empty()) {
            return std::nullopt;
        }
        deleteFaces(made.draft, pick.touchedFaces);
        break;
    case MeshOperation::Flip:
        if (pick.faces.empty()) {
            return std::nullopt;
        }
        flipFaces(made.draft, pick.faces);
        made.faces = pick.faces;
        break;
    case MeshOperation::Merge:
        if (pick.vertices.size() < 2) {
            return std::nullopt;
        }
        if (const std::optional<usize> merged = mergeVertices(made.draft, pick.vertices)) {
            made.vertices.push_back(*merged);
        }
        break;
    case MeshOperation::Split:
        if (pick.faces.empty()) {
            return std::nullopt;
        }
        splitFaces(made.draft, pick.faces);
        made.faces = pick.faces;
        break;
    case MeshOperation::Extrude:
        if (pick.faces.empty()) {
            return std::nullopt;
        }
        made.faces = extrudeFaces(made.draft, pick.faces);
        break;
    case MeshOperation::Inset:
        if (pick.faces.empty()) {
            return std::nullopt;
        }
        made.faces = insetFaces(made.draft, pick.faces);
        break;
    case MeshOperation::Subdivide:
        if (pick.faces.empty()) {
            return std::nullopt;
        }
        made.faces = subdivideFaces(made.draft, pick.faces);
        break;
    }
    return made;
}

std::vector<OperatedRoom> operateMesh(const SceneMesh& mesh, const MeshOperation operation,
                                      const std::span<const MeshPick> picks) {
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr) {
            continue;
        }
        if (std::optional<OperatedRoom> made = operateDraft(makeRoomDraft(*room), operation, pick)) {
            operated.push_back(std::move(*made));
        }
    }
    return operated;
}

} // namespace toki::scene
