#include "MeshEditor.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <array>
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

} // namespace

void MeshEditor::setTarget(const EditTarget next) {
    if (next != target) {
        target = next;
        readImages.clear();
        selection.clear();
    }
}

void MeshEditor::reset() {
    readImages.clear();
    sceneMesh.reset();
    editMesh.reset();
    selection.clear();
}

void MeshEditor::read(const Scene& scene, const SceneView& view) {
    readImages = scene.images;
    readLayer = view.layer;
    readHiddenRooms = view.hiddenRooms;
    isOfferStale = true;
    if (target == EditTarget::Collision) {
        const Collision* collision = findLayerCollision(scene, view.layer);
        editMesh = collision != nullptr ? makeCollisionEdit(*collision) : EditMesh{};
        selection.keepWithin(*editMesh);
        return;
    }
    const SceneMesh* kept = sceneMesh && sceneMesh->layer == view.layer ? &*sceneMesh : nullptr;
    SceneMesh read = readSceneMesh(scene, view.layer, kept);
    for (const base::Diagnostic& diagnostic : read.diagnostics) {
        core::logWarn("{}: {}", scene.name, diagnostic.message);
    }
    usize roomCount = 0;
    if (view.layer < scene.layers.size()) {
        roomCount = scene.layers[view.layer].rooms.size();
    }
    editMesh = makeMeshEdit(read, roomCount);
    sceneMesh = std::move(read);
    selection.keepWithin(*editMesh);
}

void MeshEditor::offer(const Scene& scene, const SceneView& view,
                       viewport::ViewportOverlay& overlay) {
    if (readImages != scene.images || readLayer != view.layer || !editMesh) {
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
    viewportPanel.setTransformTarget(std::nullopt, nullptr);
    overlay.faces.insert(overlay.faces.end(), componentOverlay.faces.begin(),
                         componentOverlay.faces.end());
    overlay.edges.insert(overlay.edges.end(), componentOverlay.edges.begin(),
                         componentOverlay.edges.end());
    overlay.points.insert(overlay.points.end(), componentOverlay.points.begin(),
                          componentOverlay.points.end());
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
    for (usize piece = 0; piece < editMesh->pieces.size(); ++piece) {
        if (!isPieceShown(readHiddenRooms, target, piece)) {
            continue;
        }
        const EditPiece& shown = editMesh->pieces[piece];
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
