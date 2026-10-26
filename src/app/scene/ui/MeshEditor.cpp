#include "MeshEditor.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

using viewport::SelectionMode;

constexpr ImVec4 EdgeColor{0.08F, 0.08F, 0.10F, 0.85F};
constexpr ImVec4 VertexColor{0.05F, 0.05F, 0.06F, 1.0F};
constexpr f32 SelectedFaceAlpha = 0.3F;
constexpr f32 EdgeWidth = 1.0F;
constexpr f32 SelectedEdgeWidth = 2.0F;
constexpr f32 VertexSize = 5.0F;

[[nodiscard]] bool isPieceShown(const std::vector<bool>& hiddenRooms, const EditTarget target,
                                const usize piece) {
    return target == EditTarget::Collision || piece >= hiddenRooms.size() || !hiddenRooms[piece];
}

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

} // namespace

void MeshEditor::setTarget(const EditTarget next) {
    if (next != target) {
        target = next;
        readImages.clear();
        selection.clear();
    }
}

void MeshEditor::reset() {
    shownScene = nullptr;
    readImages.clear();
    sceneMesh.reset();
    collision.reset();
    editMesh.reset();
    shownMesh.reset();
    madeWrites.clear();
    madeMesh.reset();
    selection.clear();
}

void MeshEditor::read(const Scene& scene, const SceneView& view) {
    readImages = scene.images;
    readLayer = view.layer;
    readHiddenRooms = view.hiddenRooms;
    isOfferStale = true;
    const Collision* found = findLayerCollision(scene, view.layer);
    collision = found != nullptr ? std::optional<Collision>(*found) : std::nullopt;
    if (target == EditTarget::Mesh || (isCollisionLinked && collision)) {
        if (madeMesh && madeMesh->layer == view.layer && hasSymbolWrites(scene, madeWrites)) {
            sceneMesh = std::move(madeMesh);
        } else {
            const SceneMesh* kept =
                sceneMesh && sceneMesh->layer == view.layer ? &*sceneMesh : nullptr;
            SceneMesh read = readSceneMesh(scene, view.layer, kept);
            if (kept == nullptr) {
                for (const base::Diagnostic& diagnostic : read.diagnostics) {
                    core::logWarn("{}: {}", scene.name, diagnostic.message);
                }
            }
            sceneMesh = std::move(read);
        }
    }
    madeMesh.reset();
    madeWrites.clear();
    if (target == EditTarget::Collision) {
        editMesh = collision ? makeCollisionEdit(*collision) : EditMesh{};
    } else {
        const usize roomCount =
            view.layer < scene.layers.size() ? scene.layers[view.layer].rooms.size() : 0;
        editMesh = makeMeshEdit(*sceneMesh, roomCount);
    }
    shownMesh = editMesh;
    selection.keepWithin(*editMesh);
}

void MeshEditor::offer(const Scene& scene, const SceneView& view,
                       viewport::ViewportOverlay& overlay) {
    shownScene = &scene;
    const bool needsMesh = target == EditTarget::Mesh || isCollisionLinked;
    if (readImages != scene.images || readLayer != view.layer || !editMesh ||
        (needsMesh && collision && !sceneMesh)) {
        read(scene, view);
    }
    if (readHiddenRooms != view.hiddenRooms) {
        readHiddenRooms = view.hiddenRooms;
        isOfferStale = true;
    }
    const SelectionMode mode = viewportPanel.getSelectionMode();
    if (mode != selection.getMode()) {
        selection.setMode(mode, *editMesh);
        isOfferStale = true;
    }
    if (isOfferStale) {
        makeOffer();
        makeOverlay();
        isOfferStale = false;
    }
    viewportPanel.setPickables(pickables,
                               [this](const viewport::ViewportPanel::ViewportPick& hit,
                                      const viewport::PickMode pickMode) { pick(hit, pickMode); });
    viewportPanel.setSelectedOwners({});
    offerTransform();
    overlay.faces.insert(overlay.faces.end(), componentOverlay.faces.begin(),
                         componentOverlay.faces.end());
    overlay.edges.insert(overlay.edges.end(), componentOverlay.edges.begin(),
                         componentOverlay.edges.end());
    overlay.points.insert(overlay.points.end(), componentOverlay.points.begin(),
                          componentOverlay.points.end());
}

void MeshEditor::offerTransform() {
    transformed = selection.listVertices(*editMesh);
    if (transformed.empty()) {
        viewportPanel.setTransformTarget(std::nullopt, nullptr);
        return;
    }
    viewport::TransformTarget transformTarget;
    glm::vec3 sum(0.0F);
    for (const ComponentRef& vertex : transformed) {
        const glm::vec3& position = editMesh->pieces[vertex.piece].vertices[vertex.index];
        transformTarget.points.push_back(position);
        sum += position;
    }
    transformTarget.pivot = sum / static_cast<f32>(transformed.size());
    viewportPanel.setTransformTarget(
        std::move(transformTarget),
        [this](const viewport::TransformEdit& edit) { commit(edit.points); },
        [this](const std::optional<viewport::TransformEdit>& edit) { preview(edit); });
}

MeshEditor::Moves MeshEditor::makeMoves(const std::span<const glm::vec3> points) const {
    Moves moves;
    for (usize i = 0; i < transformed.size() && i < points.size(); ++i) {
        const ComponentRef& vertex = transformed[i];
        if (target == EditTarget::Mesh) {
            moves.mesh.push_back(
                {.room = vertex.piece, .vertex = vertex.index, .position = points[i]});
        } else {
            moves.collision.push_back(
                {.vertex = vertex.index,
                 .position = {roundToS16(points[i].x), roundToS16(points[i].y),
                              roundToS16(points[i].z)}});
        }
    }
    if (!isCollisionLinked || !collision || !sceneMesh) {
        return moves;
    }
    if (target == EditTarget::Mesh) {
        moves.collision = findLinkedCollisionMoves(*collision, *sceneMesh, moves.mesh);
    } else {
        moves.mesh = findLinkedMeshMoves(*collision, *sceneMesh, moves.collision);
    }
    return moves;
}

std::vector<SymbolWrite> MeshEditor::makeWrites(const Moves& moves) const {
    std::vector<SymbolWrite> writes;
    if (shownScene == nullptr) {
        return writes;
    }
    if (sceneMesh && !moves.mesh.empty()) {
        writes = makeMeshMoveWrites(*shownScene, *sceneMesh, moves.mesh);
    }
    if (collision && !moves.collision.empty()) {
        std::ranges::move(makeCollisionMoveWrites(*shownScene, *collision, moves.collision),
                          std::back_inserter(writes));
    }
    return writes;
}

void MeshEditor::commit(const std::span<const glm::vec3> points) {
    const Moves moves = makeMoves(points);
    std::vector<SymbolWrite> writes = makeWrites(moves);
    if (writes.empty()) {
        return;
    }
    madeWrites = writes;
    madeMesh.reset();
    if (sceneMesh) {
        madeMesh = makeMovedMesh(*sceneMesh, moves.mesh);
    }
    applyEdit([writes = std::move(writes)](SceneModel& model) {
        return applySymbolWrites(model, writes);
    });
}

void MeshEditor::preview(const std::optional<viewport::TransformEdit>& edit) {
    isOfferStale = true;
    shownMesh = editMesh;
    if (!edit) {
        showPreview(std::nullopt);
        return;
    }
    for (usize i = 0; i < transformed.size() && i < edit->points.size(); ++i) {
        shownMesh->pieces[transformed[i].piece].vertices[transformed[i].index] = edit->points[i];
    }
    const Moves moves = makeMoves(edit->points);
    Preview shown{.writes = makeWrites(moves)};
    if (collision) {
        shown.collision = makeMovedCollision(*collision, moves.collision);
    }
    showPreview(shown);
}

void MeshEditor::pick(const viewport::ViewportPanel::ViewportPick& hit,
                      const viewport::PickMode pickMode) {
    std::vector<ComponentRef> picked;
    for (const usize index : hit.points) {
        picked.push_back(pickedPoints[index]);
    }
    for (const usize index : hit.segments) {
        picked.push_back(pickedSegments[index]);
    }
    for (const usize index : hit.boxedPoints) {
        picked.push_back(pickedBoxes[index]);
    }
    if (hit.ray && selection.getMode() == SelectionMode::Face && editMesh) {
        std::vector<bool> isShown;
        for (usize piece = 0; piece < editMesh->pieces.size(); ++piece) {
            isShown.push_back(isPieceShown(readHiddenRooms, target, piece));
        }
        if (const std::optional<ComponentRef> face =
                findHitFace(*editMesh, hit.ray->origin, hit.ray->direction, isShown)) {
            picked.push_back(*face);
        }
    }
    selection.pick(picked, pickMode);
    isOfferStale = true;
}

void MeshEditor::makeOffer() {
    pickables = {};
    pickedPoints.clear();
    pickedSegments.clear();
    pickedBoxes.clear();
    const SelectionMode mode = selection.getMode();
    for (usize piece = 0; piece < editMesh->pieces.size(); ++piece) {
        if (!isPieceShown(readHiddenRooms, target, piece)) {
            continue;
        }
        const EditPiece& shown = editMesh->pieces[piece];
        if (mode == SelectionMode::Vertex) {
            for (usize vertex = 0; vertex < shown.vertices.size(); ++vertex) {
                pickables.points.push_back(shown.vertices[vertex]);
                pickedPoints.push_back({.piece = piece, .index = vertex});
            }
        } else if (mode == SelectionMode::Edge) {
            for (usize edge = 0; edge < shown.edges.size(); ++edge) {
                const std::array<usize, 2>& ends = shown.edges[edge].vertices;
                pickables.segments.push_back({shown.vertices[ends[0]], shown.vertices[ends[1]]});
                pickedSegments.push_back({.piece = piece, .index = edge});
            }
        } else {
            for (usize face = 0; face < shown.faces.size(); ++face) {
                pickables.boxedPoints.push_back(findFaceMiddle(shown, face));
                pickedBoxes.push_back({.piece = piece, .index = face});
            }
        }
    }
}

void MeshEditor::makeOverlay() {
    componentOverlay = {};
    const SelectionMode mode = selection.getMode();
    const u32 edgeColor = ImGui::GetColorU32(EdgeColor);
    const u32 selectedColor = ImGui::GetColorU32(core::palette::Accent);
    ImVec4 faceFill = core::palette::Accent;
    faceFill.w = SelectedFaceAlpha;
    const u32 faceColor = ImGui::GetColorU32(faceFill);
    const u32 vertexColor = ImGui::GetColorU32(VertexColor);
    for (usize piece = 0; piece < shownMesh->pieces.size(); ++piece) {
        if (!isPieceShown(readHiddenRooms, target, piece)) {
            continue;
        }
        const EditPiece& shown = shownMesh->pieces[piece];
        for (usize edge = 0; edge < shown.edges.size(); ++edge) {
            const std::array<usize, 2>& ends = shown.edges[edge].vertices;
            const bool isSelected =
                mode == SelectionMode::Edge && selection.contains({.piece = piece, .index = edge});
            componentOverlay.edges.push_back({.from = shown.vertices[ends[0]],
                                              .to = shown.vertices[ends[1]],
                                              .color = isSelected ? selectedColor : edgeColor,
                                              .width = isSelected ? SelectedEdgeWidth : EdgeWidth});
        }
        if (mode == SelectionMode::Face) {
            for (usize face = 0; face < shown.faces.size(); ++face) {
                if (!selection.contains({.piece = piece, .index = face})) {
                    continue;
                }
                const std::array<usize, 3>& corners = shown.faces[face];
                componentOverlay.faces.push_back(
                    {.corners = {shown.vertices[corners[0]], shown.vertices[corners[1]],
                                 shown.vertices[corners[2]]},
                     .color = faceColor});
                for (usize corner = 0; corner < 3; ++corner) {
                    componentOverlay.edges.push_back(
                        {.from = shown.vertices[corners[corner]],
                         .to = shown.vertices[corners[(corner + 1) % 3]],
                         .color = selectedColor,
                         .width = SelectedEdgeWidth});
                }
            }
        }
        if (mode == SelectionMode::Vertex) {
            for (usize vertex = 0; vertex < shown.vertices.size(); ++vertex) {
                const bool isSelected = selection.contains({.piece = piece, .index = vertex});
                componentOverlay.points.push_back(
                    {.position = shown.vertices[vertex],
                     .color = isSelected ? selectedColor : vertexColor,
                     .size = VertexSize});
            }
        }
    }
}

} // namespace toki::scene
