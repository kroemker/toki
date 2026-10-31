#include "MeshEditor.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/domain/Log.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionGeometry.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/ComponentSummary.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MaterialEdits.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshFill.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneSurface.h"
#include "app/scene/domain/SceneView.h"
#include "app/scene/domain/SurfaceEdits.h"
#include "app/scene/domain/SurfaceOperations.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

using viewport::SelectionMode;

constexpr ImVec4 EdgeColor{0.08F, 0.08F, 0.10F, 0.85F};
constexpr ImVec4 VertexColor{0.05F, 0.05F, 0.06F, 1.0F};
constexpr f32 SelectedFaceAlpha = 0.3F;
constexpr f32 MaterialFaceAlpha = 0.18F;
constexpr f32 EdgeWidth = 1.0F;
constexpr f32 SelectedEdgeWidth = 2.0F;
constexpr f32 VertexSize = 5.0F;
constexpr f32 MenuWrapEms = 24.0F;
constexpr usize PastMeshCount = 32;

[[nodiscard]] bool isRoomShown(const std::vector<bool>& hiddenRooms, const usize room) {
    return room >= hiddenRooms.size() || !hiddenRooms[room];
}

[[nodiscard]] const char* findMissingPick(const MeshOperation operation) {
    switch (operation) {
    case MeshOperation::Merge:
        return "Pick two vertices or more.";
    case MeshOperation::Delete:
        return "Pick something to take out.";
    default:
        return "Pick a face, or the vertices or edges around one.";
    }
}

} // namespace

void MeshEditor::reset() {
    shownScene = nullptr;
    readImages.clear();
    sceneMesh.reset();
    materials.clear();
    collision.reset();
    surface.reset();
    editMesh.reset();
    shownMesh.reset();
    madeWrites.clear();
    madeMesh.reset();
    keptMesh.reset();
    pastMeshes.clear();
    wantedFaces.clear();
    wantedVertices.clear();
    wantedPolygons.clear();
    wantedTransform.reset();
    flatSolidFaces.clear();
    failure.clear();
    selection.clear();
    selectedMaterial.reset();
    isMaterialOverlayStale = true;
}

void MeshEditor::read(const Scene& scene, const SceneView& view) {
    const bool isFirstRead = !sceneMesh;
    readImages = scene.images;
    readLayer = view.layer;
    const bool hasLayer = view.layer < scene.layers.size();
    readGameLayer = hasLayer ? scene.layers[view.layer].index : 0;
    roomCount = hasLayer ? scene.layers[view.layer].rooms.size() : 0;
    readHiddenRooms = view.hiddenRooms;
    readShowsCollision = view.showsCollision;
    isOfferStale = true;
    const Collision* found = findLayerCollision(scene, view.layer);
    collision = found != nullptr ? std::optional<Collision>(*found) : std::nullopt;
    if (sceneMesh) {
        pastMeshes.push_front(std::move(*sceneMesh));
        sceneMesh.reset();
        if (pastMeshes.size() > PastMeshCount) {
            pastMeshes.pop_back();
        }
    }
    if (madeMesh && madeMesh->layer == view.layer && hasSymbolWrites(scene, madeWrites)) {
        sceneMesh = std::move(madeMesh);
    } else {
        std::vector<const SceneMesh*> kept;
        if (keptMesh) {
            kept.push_back(&*keptMesh);
        }
        for (const SceneMesh& past : pastMeshes) {
            kept.push_back(&past);
        }
        SceneMesh read = readSceneMesh(scene, view.layer, kept);
        if (isFirstRead) {
            for (const base::Diagnostic& diagnostic : read.diagnostics) {
                core::logWarn("{}: {}", scene.name, diagnostic.message);
            }
        }
        sceneMesh = std::move(read);
    }
    madeMesh.reset();
    madeWrites.clear();
    keptMesh.reset();
    surface.reset();
    if (collision) {
        surface = pairSurface(*sceneMesh, *collision);
        sceneMesh = makeSolidMesh(std::move(*sceneMesh), *surface, *collision);
    }
    materials = listSceneMaterials(scene, *sceneMesh);
    resolvedBindings.assign(materials.size(), std::nullopt);
    if (collision) {
        resolvedBindings =
            resolveMaterialBindings(scene.materialBindings, *collision, *sceneMesh, materials);
    }
    for (usize material = 0; isFirstRead && material < resolvedBindings.size(); ++material) {
        if (const std::optional<ResolvedBinding>& resolved = resolvedBindings[material]) {
            if (!resolved->collision) {
                core::logWarn("{}: material {} is bound to collision that is not there: {}. Bind "
                              "it again or unbind it in its menu.",
                              scene.name, materials[material].name, resolved->collision.error());
            } else if (resolved->unbound > 0) {
                core::logWarn("{}: {} faces of material {} are not made of the collision it is "
                              "bound to. Re-apply or unbind the binding in its menu.",
                              scene.name, resolved->unbound, materials[material].name);
            }
        }
    }
    editMesh = collision ? makeSurfaceEdit(*sceneMesh, roomCount, *surface, *collision)
                         : makeMeshEdit(*sceneMesh, roomCount);
    shownMesh = editMesh;
    selection.keepWithin(*editMesh);
    pickWanted();
}

void MeshEditor::pickWanted() {
    if (wantedFaces.empty() && wantedVertices.empty() && wantedPolygons.empty()) {
        return;
    }
    if (sceneMesh && editMesh) {
        const std::set<std::array<MeshEntryKey, 3>> faces(wantedFaces.begin(), wantedFaces.end());
        const std::set<MeshEntryKey> vertices(wantedVertices.begin(), wantedVertices.end());
        std::vector<ComponentRef> pickedFaces;
        std::vector<ComponentRef> pickedVertices;
        for (const RoomMesh& room : sceneMesh->rooms) {
            if (room.room >= editMesh->pieces.size()) {
                continue;
            }
            const EditPiece& piece = editMesh->pieces[room.room];
            for (usize face = 0; face < piece.faces.size(); ++face) {
                const MeshFace& source = room.faces[findFaceSource(piece, face)];
                std::array<MeshEntryKey, 3> keys;
                for (usize corner = 0; corner < 3; ++corner) {
                    keys[corner] = room.entries[source.corners[corner]].key;
                }
                if (faces.contains(keys)) {
                    pickedFaces.push_back({.piece = room.room, .index = face});
                }
            }
            for (usize vertex = 0; vertex < room.vertices.size(); ++vertex) {
                if (std::ranges::any_of(room.vertices[vertex].entries, [&](const usize entry) {
                        return vertices.contains(room.entries[entry].key);
                    })) {
                    pickedVertices.push_back({.piece = room.room, .index = vertex});
                }
            }
        }
        if (roomCount < editMesh->pieces.size()) {
            std::set<std::array<std::array<s16, 3>, 3>> polygons;
            for (std::array<std::array<s16, 3>, 3> corners : wantedPolygons) {
                std::ranges::sort(corners);
                polygons.insert(corners);
            }
            const EditPiece& piece = editMesh->pieces[roomCount];
            for (usize face = 0; face < piece.faces.size(); ++face) {
                std::array<std::array<s16, 3>, 3> corners{};
                for (usize corner = 0; corner < 3; ++corner) {
                    corners[corner] = roundToCollision(piece.vertices[piece.faces[face][corner]]);
                }
                std::ranges::sort(corners);
                if (polygons.contains(corners)) {
                    pickedFaces.push_back({.piece = roomCount, .index = face});
                }
            }
        }
        if (!pickedFaces.empty()) {
            selection.selectFaces(pickedFaces, *editMesh);
        } else {
            selection.selectVertices(pickedVertices, *editMesh);
        }
        isOfferStale = true;
    }
    wantedFaces.clear();
    wantedVertices.clear();
    wantedPolygons.clear();
    if (wantedTransform && !selection.isEmpty()) {
        viewportPanel.startTransform(*wantedTransform);
    }
    wantedTransform.reset();
}

std::vector<MeshPick> MeshEditor::makePicks() const {
    std::map<usize, MeshPick> picks;
    const auto add = [this, &picks](const std::vector<ComponentRef>& refs,
                                    std::vector<usize> MeshPick::*list, const bool isFace) {
        for (const ComponentRef& ref : refs) {
            MeshPick& pick = picks[ref.piece];
            pick.room = ref.piece;
            (pick.*list)
                .push_back(isFace ? findFaceSource(editMesh->pieces[ref.piece], ref.index)
                                  : ref.index);
        }
    };
    add(selection.listFaces(*editMesh), &MeshPick::faces, true);
    add(selection.listTouchedFaces(*editMesh), &MeshPick::touchedFaces, true);
    add(selection.listVertices(*editMesh), &MeshPick::vertices, false);
    std::vector<MeshPick> listed;
    for (auto& [room, pick] : picks) {
        listed.push_back(std::move(pick));
    }
    return listed;
}

std::pair<std::vector<MeshPick>, std::optional<MeshPick>>
MeshEditor::splitPicks(std::vector<MeshPick> picks) const {
    std::optional<MeshPick> solidOnly;
    std::erase_if(picks, [this, &solidOnly](MeshPick& pick) {
        if (pick.room < roomCount) {
            return false;
        }
        solidOnly = std::move(pick);
        return true;
    });
    return {std::move(picks), std::move(solidOnly)};
}

std::vector<ComponentRef>
MeshEditor::findMeshFaces(const std::span<const ComponentRef> faces) const {
    std::vector<ComponentRef> found;
    for (const ComponentRef& face : faces) {
        if (face.piece < roomCount && face.piece < editMesh->pieces.size()) {
            found.push_back({.piece = face.piece,
                             .index = findFaceSource(editMesh->pieces[face.piece], face.index)});
        }
    }
    return found;
}

bool MeshEditor::canOperate(const MeshOperation operation) const {
    if (!sceneMesh || !editMesh || shownScene == nullptr || selection.isEmpty()) {
        return false;
    }
    switch (operation) {
    case MeshOperation::Delete:
        return true;
    case MeshOperation::Merge:
        return selection.listVertices(*editMesh).size() >= 2;
    case MeshOperation::Flip:
    case MeshOperation::Split:
    case MeshOperation::Extrude:
    case MeshOperation::Inset:
    case MeshOperation::Subdivide:
        return !selection.listFaces(*editMesh).empty();
    }
    return false;
}

void MeshEditor::operate(const MeshOperation operation) {
    if (!canOperate(operation)) {
        return;
    }
    std::optional<viewport::TransformKind> then;
    if (operation == MeshOperation::Extrude) {
        then = viewport::TransformKind::Move;
    } else if (operation == MeshOperation::Inset) {
        then = viewport::TransformKind::Scale;
    }
    auto [meshPicks, solidOnlyPick] = splitPicks(makePicks());
    std::vector<OperatedRoom> operated = operateMesh(*sceneMesh, operation, meshPicks);
    if (solidOnlyPick && collision && surface) {
        if (std::optional<OperatedRoom> made = operateDraft(
                makeSolidOnlyDraft(*collision, *surface, roomCount), operation, *solidOnlyPick)) {
            operated.push_back(std::move(*made));
        }
    }
    rebuild(operated, then);
}

void MeshEditor::rebuild(const std::vector<OperatedRoom>& operated,
                         const std::optional<viewport::TransformKind> then,
                         std::optional<MaterialBindings> bindings, const Collision* base) {
    failure.clear();
    if (shownScene == nullptr || !sceneMesh) {
        return;
    }
    if (base == nullptr && collision) {
        base = &*collision;
    }
    if (operated.empty()) {
        if (bindings) {
            applySurface(std::nullopt, std::nullopt, std::move(bindings));
        }
        return;
    }
    std::vector<RoomDraft> drafts;
    std::vector<std::optional<usize>> draftOf;
    const RoomDraft* solidOnly = nullptr;
    for (const OperatedRoom& room : operated) {
        if (room.draft.room >= roomCount) {
            solidOnly = &room.draft;
            draftOf.emplace_back();
        } else {
            draftOf.emplace_back(drafts.size());
            drafts.push_back(room.draft);
        }
    }
    std::optional<MeshRebuild> planned;
    if (!drafts.empty()) {
        std::expected<MeshRebuild, base::Diagnostic> made =
            planMeshRebuild(*shownScene, SceneModel(*shownScene), *sceneMesh, drafts);
        if (!made) {
            failure = made.error().message;
            core::logWarn("{}: {}", shownScene->name, failure);
            return;
        }
        if (!made->isEmpty()) {
            planned = std::move(*made);
        }
    }
    std::optional<Collision> drafted;
    if (base != nullptr && surface) {
        const usize flat = countLeftOutPolygons(*base, *surface, drafts, solidOnly);
        if (flat > 0 && !then) {
            core::logWarn("{}: {} solid faces have their corners in a line at whole units, and "
                          "are left without collision",
                          shownScene->name, flat);
        }
        Collision made = makeDraftedCollision(*base, *surface, drafts, solidOnly);
        if (made.vertices != collision->vertices || made.polygons != collision->polygons) {
            drafted = std::move(made);
        }
    }
    if (!planned && !drafted) {
        if (bindings) {
            applySurface(std::nullopt, std::nullopt, std::move(bindings));
        }
        return;
    }
    wantedFaces.clear();
    wantedVertices.clear();
    wantedPolygons.clear();
    flatSolidFaces.clear();
    for (usize index = 0; index < operated.size(); ++index) {
        const OperatedRoom& room = operated[index];
        if (!draftOf[index]) {
            for (const usize face : room.faces) {
                std::array<std::array<s16, 3>, 3> corners{};
                for (usize corner = 0; corner < 3; ++corner) {
                    corners[corner] = roundToCollision(
                        findCornerPosition(room.draft, room.draft.faces[face], corner));
                }
                wantedPolygons.push_back(corners);
            }
            continue;
        }
        if (!planned) {
            continue;
        }
        const usize draft = *draftOf[index];
        for (const usize face : room.faces) {
            wantedFaces.push_back(planned->faces[draft][face]);
        }
        for (const usize face : listFlatSolidFaces(drafts[draft])) {
            flatSolidFaces.push_back({.corners = planned->faces[draft][face],
                                      .material = *drafts[draft].faces[face].collision});
        }
        for (const usize vertex : room.vertices) {
            for (usize face = 0; face < drafts[draft].faces.size(); ++face) {
                for (usize corner = 0; corner < 3; ++corner) {
                    if (drafts[draft].faces[face].corners[corner].vertex == vertex) {
                        wantedVertices.push_back(planned->faces[draft][face][corner]);
                    }
                }
            }
        }
    }
    if (planned) {
        keptMesh = planned->kept;
    }
    wantedTransform = then;
    applySurface(std::move(planned), std::move(drafted), std::move(bindings));
}

void MeshEditor::applySurface(std::optional<MeshRebuild> rebuild, std::optional<Collision> made,
                              std::optional<MaterialBindings> bindings) {
    applyEdit([rebuild = std::move(rebuild), made = std::move(made), bindings = std::move(bindings),
               layer = readGameLayer](SceneModel& model) -> std::expected<void, base::Diagnostic> {
        if (bindings) {
            model.setMaterialBindings(*bindings);
        }
        if (rebuild) {
            if (std::expected<void, base::Diagnostic> applied = applyMeshRebuild(model, *rebuild);
                !applied) {
                return applied;
            }
        }
        if (made) {
            return setCollisionGeometry(model, layer, *made);
        }
        return {};
    });
}

std::vector<ComponentRef> MeshEditor::listMaterialFaces(const usize material) const {
    std::vector<ComponentRef> faces;
    if (!sceneMesh || material >= materials.size()) {
        return faces;
    }
    for (const RoomMesh& room : sceneMesh->rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (isDrawnUnder(materials[material], room.faces[face])) {
                faces.push_back({.piece = room.room, .index = face});
            }
        }
    }
    return faces;
}

void MeshEditor::pickMaterial(const usize material) {
    if (!sceneMesh || !editMesh || material >= materials.size()) {
        return;
    }
    selectFaces(listMaterialFaces(material));
}

void MeshEditor::pickCollisionMaterial(const CollisionMaterial& material) {
    if (!sceneMesh || !editMesh || !collision) {
        return;
    }
    selectFaces(listCollisionMaterialFaces(material));
}

std::vector<ComponentRef>
MeshEditor::listCollisionMaterialFaces(const CollisionMaterial& material) const {
    std::vector<ComponentRef> faces;
    if (!sceneMesh || !editMesh || !collision) {
        return faces;
    }
    for (const RoomMesh& room : sceneMesh->rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (room.faces[face].collision == material) {
                faces.push_back({.piece = room.room, .index = face});
            }
        }
    }
    if (roomCount < editMesh->pieces.size()) {
        const EditPiece& solidOnly = editMesh->pieces[roomCount];
        for (usize face = 0; face < solidOnly.faces.size(); ++face) {
            const usize polygon = findFaceSource(solidOnly, face);
            if (polygon < collision->polygons.size() &&
                getPolygonMaterial(collision->polygons[polygon]) == material) {
                faces.push_back({.piece = roomCount, .index = face});
            }
        }
    }
    return faces;
}

void MeshEditor::selectMaterial(const MaterialRef& material) {
    selectedMaterial = material;
    isMaterialOverlayStale = true;
}

void MeshEditor::clearSelectedMaterial() {
    if (selectedMaterial) {
        selectedMaterial.reset();
        isMaterialOverlayStale = true;
    }
}

void MeshEditor::addMaterialOverlay(viewport::ViewportOverlay& overlay) {
    if (isMaterialOverlayStale) {
        makeMaterialOverlay();
        isMaterialOverlayStale = false;
    }
    overlay.faces.insert(overlay.faces.end(), materialOverlay.faces.begin(),
                         materialOverlay.faces.end());
}

void MeshEditor::makeMaterialOverlay() {
    materialOverlay = {};
    if (!selectedMaterial || !editMesh) {
        return;
    }
    const auto* render = std::get_if<RenderMaterialRef>(&*selectedMaterial);
    const std::vector<ComponentRef> faces =
        render != nullptr
            ? listMaterialFaces(render->material)
            : listCollisionMaterialFaces(std::get<CollisionMaterial>(*selectedMaterial));
    ImVec4 fill = core::palette::Accent;
    fill.w = MaterialFaceAlpha;
    const u32 color = ImGui::GetColorU32(fill);
    for (const ComponentRef& face : faces) {
        const bool isShown = face.piece < roomCount ? isRoomShown(readHiddenRooms, face.piece) ||
                                                          (render == nullptr && readShowsCollision)
                                                    : readShowsCollision;
        if (!isShown || face.piece >= editMesh->pieces.size()) {
            continue;
        }
        const EditPiece& piece = editMesh->pieces[face.piece];
        const std::array<usize, 3>& corners = piece.faces[face.index];
        materialOverlay.faces.push_back(
            {.corners = {piece.vertices[corners[0]], piece.vertices[corners[1]],
                         piece.vertices[corners[2]]},
             .color = color});
    }
}

void MeshEditor::drawMaterialSettings(const usize material) {
    if (!sceneMesh || material >= materials.size()) {
        return;
    }
    std::optional<BindingRows> rows;
    if (collision) {
        rows = makeBindingRows(material);
    }
    if (const std::optional<std::optional<MaterialBinding>> picked =
            MaterialForm::draw(materials[material], rows)) {
        bindMaterial(material, *picked);
    }
}

BindingRows MeshEditor::makeBindingRows(const usize material) const {
    BindingRows rows;
    const std::optional<ResolvedBinding> resolved =
        material < resolvedBindings.size() ? resolvedBindings[material] : std::nullopt;
    const bool isMixed = isCollisionMixed(*sceneMesh, materials, material);
    rows.choices.push_back({.label = "Unbound"});
    const auto offer = [&](std::string label, const std::optional<CollisionMaterial>& made) {
        const MaterialBinding binding = makeMaterialBinding(*collision, made);
        if (resolved && resolved->binding == binding) {
            rows.current = rows.choices.size();
        }
        rows.choices.push_back(
            {.label = std::move(label),
             .binding = binding,
             .changed = isMixed ? countUnbound(*sceneMesh, materials, material, made) : 0});
    };
    offer("Not Solid", std::nullopt);
    for (const SceneCollisionMaterial& listed : listCollisionMaterials(*collision)) {
        offer(makeCollisionMaterialName(collisionNames, *collision, listed.material),
              listed.material);
    }
    if (!resolved) {
        return rows;
    }
    if (!resolved->collision) {
        rows.problem = std::format("Bound to collision not there: {}. Bind it again or unbind it.",
                                   resolved->collision.error());
    } else {
        rows.unbound = resolved->unbound;
    }
    if (rows.current == 0) {
        rows.current = rows.choices.size();
        rows.choices.push_back({.label = !resolved->collision ? std::string("Missing Collision")
                                         : *resolved->collision
                                             ? makeCollisionMaterialName(collisionNames, *collision,
                                                                         **resolved->collision)
                                             : std::string("Not Solid"),
                                .binding = resolved->binding});
    }
    return rows;
}

void MeshEditor::selectFaces(const std::vector<ComponentRef>& faces) {
    clearSelectedMaterial();
    viewportPanel.setSelectionMode(SelectionMode::Face);
    selection.setMode(SelectionMode::Face, *editMesh);
    selection.selectFaces(faces, *editMesh);
    isOfferStale = true;
}

std::optional<std::string> MeshEditor::findBindingName(const usize material) const {
    if (material >= resolvedBindings.size() || !resolvedBindings[material] || !collision) {
        return std::nullopt;
    }
    const std::expected<std::optional<CollisionMaterial>, std::string>& bound =
        resolvedBindings[material]->collision;
    if (!bound) {
        return "Missing Collision";
    }
    return *bound ? makeCollisionMaterialName(collisionNames, *collision, **bound)
                  : std::string("Not Solid");
}

std::optional<u16> MeshEditor::makePickedSurfaceType() const {
    std::optional<u16> found;
    for (const CollisionPolygonRef& polygon : listPickedPolygons()) {
        const u16 type = collision->polygons[polygon.polygon].type;
        if (found && *found != type) {
            return std::nullopt;
        }
        found = type;
    }
    return found;
}

void MeshEditor::assignPicked(const usize material) {
    failure.clear();
    if (!canOperate(MeshOperation::Flip) || material >= materials.size()) {
        return;
    }
    std::expected<std::vector<OperatedRoom>, base::Diagnostic> operated =
        assignMaterial(*sceneMesh, makePicks(), materials[material]);
    if (!operated) {
        failure = operated.error().message;
        core::logWarn("{}: {}", shownScene->name, failure);
        return;
    }
    rebuild(makeBoundFaces(std::move(*operated), material < resolvedBindings.size()
                                                     ? resolvedBindings[material]
                                                     : std::nullopt),
            std::nullopt);
}

void MeshEditor::bindMaterial(const usize material, const std::optional<MaterialBinding> binding) {
    if (shownScene == nullptr || !sceneMesh || material >= materials.size()) {
        return;
    }
    MaterialBindings bindings = shownScene->materialBindings;
    const std::string key = makeMaterialKey(materials[material]);
    std::vector<OperatedRoom> operated;
    if (binding) {
        bindings.materials.insert_or_assign(key, *binding);
        if (collision) {
            if (const auto bound = findBoundCollision(*collision, *binding)) {
                operated = bindFaces(*sceneMesh, materials, material, *bound);
            }
        }
    } else {
        bindings.materials.erase(key);
    }
    rebuild(operated, std::nullopt, std::move(bindings));
}

void MeshEditor::drawBindingMenu(const usize material) {
    if (!ImGui::BeginMenu("Collision", collision.has_value())) {
        ImGui::SetItemTooltip("%s", collision ? "What the faces drawn under the material are made "
                                                "of. Bound, their collision follows the material, "
                                                "and is set here alone."
                                              : "The layer has no collision.");
        return;
    }
    const std::optional<ResolvedBinding>& resolved = resolvedBindings[material];
    if (resolved) {
        if (!resolved->collision) {
            ImGui::TextDisabled("Bound to collision not there: %s",
                                resolved->collision.error().c_str());
        } else if (resolved->unbound > 0) {
            ImGui::TextDisabled("%zu faces are not as it is bound", resolved->unbound);
            if (ImGui::MenuItem("Re-apply Binding")) {
                bindMaterial(material, resolved->binding);
            }
        }
        if (ImGui::MenuItem("Unbind")) {
            bindMaterial(material, std::nullopt);
        }
        ImGui::SetItemTooltip("Leaves the faces as they are, each set on its own from now on.");
        ImGui::Separator();
    } else {
        ImGui::MenuItem("Unbound", nullptr, true, false);
    }
    const bool isMixed = isCollisionMixed(*sceneMesh, materials, material);
    const auto offer = [&](const std::string& label, const std::optional<CollisionMaterial>& made) {
        const MaterialBinding binding = makeMaterialBinding(*collision, made);
        const bool isBound = resolved && resolved->binding == binding;
        const usize changed = countUnbound(*sceneMesh, materials, material, made);
        if (isMixed && changed > 0 && !isBound) {
            if (ImGui::BeginMenu(label.c_str())) {
                if (ImGui::MenuItem(
                        std::format("Bind: {} Faces Change Their Collision", changed).c_str())) {
                    bindMaterial(material, binding);
                }
                ImGui::EndMenu();
            }
        } else if (ImGui::MenuItem(label.c_str(), nullptr, isBound)) {
            bindMaterial(material, binding);
        }
    };
    offer("Not Solid", std::nullopt);
    for (const SceneCollisionMaterial& listed : listCollisionMaterials(*collision)) {
        ImGui::PushID(static_cast<int>((listed.material.type * 16U) + listed.material.flags));
        offer(makeCollisionMaterialName(collisionNames, *collision, listed.material),
              listed.material);
        ImGui::PopID();
    }
    ImGui::EndMenu();
}

void MeshEditor::drawAssignMenu() {
    if (!ImGui::BeginMenu("Assign Material", canOperate(MeshOperation::Flip))) {
        return;
    }
    std::set<usize> rooms;
    for (const MeshPick& pick : makePicks()) {
        if (!pick.faces.empty()) {
            rooms.insert(pick.room);
        }
    }
    for (usize material = 0; material < materials.size(); ++material) {
        const bool isDrawn = std::ranges::all_of(rooms, [&](const usize room) {
            const MaterialRoom* drawn = materials[material].findRoom(room);
            return drawn != nullptr && drawn->sample.has_value();
        });
        ImGui::PushID(static_cast<int>(material));
        if (ImGui::MenuItem(materials[material].name.c_str(), nullptr, false, isDrawn)) {
            assignPicked(material);
        }
        if (!isDrawn) {
            ImGui::SetItemTooltip("Not drawn in every room the picked faces are in. A material is "
                                  "not copied between rooms yet.");
        }
        ImGui::PopID();
    }
    ImGui::EndMenu();
}

const RoomMesh* MeshEditor::findShapeRoom() const {
    if (!sceneMesh) {
        return nullptr;
    }
    if (editMesh) {
        const std::vector<ComponentRef> faces = findMeshFaces(selection.listFaces(*editMesh));
        if (!faces.empty()) {
            return findRoomMesh(*sceneMesh, faces.front().piece);
        }
    }
    for (const RoomMesh& room : sceneMesh->rooms) {
        if (!room.faces.empty() && isRoomShown(readHiddenRooms, room.room)) {
            return &room;
        }
    }
    return nullptr;
}

void MeshEditor::addShape(const MeshShape shape) {
    failure.clear();
    const RoomMesh* room = findShapeRoom();
    if (room == nullptr || !editMesh) {
        return;
    }
    OperatedRoom made{.draft = makeRoomDraft(*room)};
    usize like = 0;
    for (const ComponentRef& face : findMeshFaces(selection.listFaces(*editMesh))) {
        if (face.piece == room->room) {
            like = face.index;
            break;
        }
    }
    const std::array<f32, 3> looked = viewportPanel.getCameraPose().target;
    const glm::vec3 center(std::round(looked[0]), std::round(looked[1]), std::round(looked[2]));
    made.faces = addMeshShape(made.draft, shape, center, ShapeSize, made.draft.faces[like]);
    rebuild({made}, std::nullopt);
}

void MeshEditor::fill() {
    failure.clear();
    if (!sceneMesh || !editMesh) {
        return;
    }
    auto [meshPicks, solidOnlyPick] = splitPicks(makePicks());
    std::erase_if(meshPicks, [](const MeshPick& pick) { return pick.vertices.empty(); });
    if (solidOnlyPick && solidOnlyPick->vertices.empty()) {
        solidOnlyPick.reset();
    }
    const usize picked = meshPicks.size() + (solidOnlyPick ? 1 : 0);
    if (picked != 1) {
        failure = picked == 0 ? "Pick three vertices or more."
                              : "Pick vertices of one room: a face is drawn by one room alone.";
        return;
    }
    const MeshPick& pick = solidOnlyPick ? *solidOnlyPick : meshPicks.front();
    OperatedRoom made;
    if (solidOnlyPick) {
        if (!collision || !surface) {
            return;
        }
        made.draft = makeSolidOnlyDraft(*collision, *surface, roomCount);
    } else if (const RoomMesh* room = findRoomMesh(*sceneMesh, pick.room)) {
        made.draft = makeRoomDraft(*room);
    } else {
        return;
    }
    std::expected<std::vector<usize>, std::string> faces =
        fillVertices(made.draft, pick.vertices, viewportPanel.getCameraEye());
    if (!faces) {
        failure = faces.error();
        return;
    }
    made.faces = std::move(*faces);
    rebuild({made}, std::nullopt);
}

const std::vector<ComponentRef>& MeshEditor::listOffered() const {
    switch (selection.getMode()) {
    case SelectionMode::Vertex:
        return pickedPoints;
    case SelectionMode::Edge:
        return pickedSegments;
    default:
        return pickedBoxes;
    }
}

void MeshEditor::selectAll() {
    selection.pick(listOffered(), viewport::PickMode::Replace);
    clearSelectedMaterial();
    isOfferStale = true;
}

void MeshEditor::invertSelection() {
    std::vector<ComponentRef> inverted;
    for (const ComponentRef& component : listOffered()) {
        if (!selection.contains(component)) {
            inverted.push_back(component);
        }
    }
    selection.pick(inverted, viewport::PickMode::Replace);
    clearSelectedMaterial();
    isOfferStale = true;
}

void MeshEditor::moveToRoom(const usize room) {
    failure.clear();
    if (!canOperate(MeshOperation::Flip)) {
        return;
    }
    const std::expected<std::vector<OperatedRoom>, base::Diagnostic> operated =
        moveFacesToRoom(*sceneMesh, makePicks(), room, materials);
    if (!operated) {
        failure = operated.error().message;
        core::logWarn("{}: {}", shownScene->name, failure);
        return;
    }
    rebuild(*operated, std::nullopt);
}

void MeshEditor::drawRoomMenu() {
    if (!ImGui::BeginMenu("Move to Room", canOperate(MeshOperation::Flip))) {
        return;
    }
    for (const RoomMesh& room : sceneMesh->rooms) {
        const std::string label = std::format("Room {}", room.room);
        if (ImGui::MenuItem(label.c_str(), nullptr, false, !room.faces.empty())) {
            moveToRoom(room.room);
        }
        ImGui::SetItemTooltip("Take the picked faces out of their rooms and draw them in room "
                              "%zu, each in the run that draws its material there.",
                              room.room);
    }
    ImGui::EndMenu();
}

void MeshEditor::drawProperties() {
    if (!hasPicked()) {
        return;
    }
    const std::vector<ComponentRef> vertices = selection.listVertices(*editMesh);
    std::vector<ComponentRef> meshVertices = vertices;
    std::erase_if(meshVertices,
                  [this](const ComponentRef& vertex) { return vertex.piece >= roomCount; });
    const ComponentSummary summary = summarizeComponents(
        *sceneMesh, materials, meshVertices, findMeshFaces(selection.listFaces(*editMesh)));
    std::vector<SceneCollisionMaterial> listed;
    std::optional<SurfaceRows> surfaceRows;
    if (collision && surface) {
        listed = listCollisionMaterials(*collision);
        surfaceRows = makeSurfaceRows(listed);
    }
    const SelectionMode mode = selection.getMode();
    const ComponentEdit edit = componentForm.draw(mode, summary, materials, surfaceRows);
    if (mode == SelectionMode::Face && summary.faces > 0) {
        if (summary.material) {
            drawMaterialSettings(*summary.material);
        } else {
            MaterialForm::drawMixed();
        }
    }
    editSurface(edit, listed);
    if (edit.material) {
        assignPicked(*edit.material);
    }
    if (edit.middle) {
        transformed = vertices;
        std::vector<glm::vec3> points;
        for (const ComponentRef& vertex : vertices) {
            points.push_back(editMesh->pieces[vertex.piece].vertices[vertex.index] +
                             (*edit.middle - summary.middle));
        }
        commit(points);
    }
    if (edit.color || edit.alpha) {
        std::vector<SymbolWrite> writes =
            makeColorWrites(*sceneMesh, meshVertices, edit.color, edit.alpha);
        if (!writes.empty()) {
            applyEdit([writes = std::move(writes)](SceneModel& model) {
                return applySymbolWrites(model, writes);
            });
        }
    }
}

SurfaceRows MeshEditor::makeSurfaceRows(const std::vector<SceneCollisionMaterial>& listed) const {
    SurfaceRows rows;
    std::set<CollisionMaterial> used;
    for (const ComponentRef& face : findMeshFaces(selection.listFaces(*editMesh))) {
        const RoomMesh* room = findRoomMesh(*sceneMesh, face.piece);
        if (room == nullptr || face.index >= room->faces.size()) {
            continue;
        }
        ++rows.drawn;
        const std::optional<usize> material = findFaceMaterial(materials, room->faces[face.index]);
        if (material && *material < resolvedBindings.size() && resolvedBindings[*material] &&
            resolvedBindings[*material]->collision) {
            ++rows.bound;
        }
        if (const std::optional<CollisionMaterial>& made = room->faces[face.index].collision) {
            ++rows.solid;
            used.insert(*made);
        }
    }
    for (const ComponentRef& face : selection.listFaces(*editMesh)) {
        if (face.piece < roomCount) {
            continue;
        }
        const usize polygon = findFaceSource(editMesh->pieces[face.piece], face.index);
        if (polygon < collision->polygons.size()) {
            ++rows.solidOnly;
            used.insert(getPolygonMaterial(collision->polygons[polygon]));
        }
    }
    for (const SceneCollisionMaterial& material : listed) {
        rows.names.push_back(
            makeCollisionMaterialName(collisionNames, *collision, material.material));
    }
    if (used.size() == 1) {
        const auto found =
            std::ranges::find(listed, *used.begin(), &SceneCollisionMaterial::material);
        if (found != listed.end()) {
            rows.material = static_cast<usize>(found - listed.begin());
        }
    }
    return rows;
}

bool MeshEditor::hasPickedBound() const {
    if (!sceneMesh || !editMesh) {
        return false;
    }
    return std::ranges::any_of(
        findMeshFaces(selection.listFaces(*editMesh)), [this](const ComponentRef& face) {
            const RoomMesh* room = findRoomMesh(*sceneMesh, face.piece);
            if (room == nullptr || face.index >= room->faces.size()) {
                return false;
            }
            const std::optional<usize> material =
                findFaceMaterial(materials, room->faces[face.index]);
            return material && *material < resolvedBindings.size() && resolvedBindings[*material] &&
                   resolvedBindings[*material]->collision;
        });
}

std::optional<usize> MeshEditor::findNearestRoom(const RoomDraft& solidOnly,
                                                 const std::span<const usize> faces) const {
    if (faces.empty() || faces.front() >= solidOnly.faces.size()) {
        return std::nullopt;
    }
    glm::vec3 center(0.0F);
    for (usize corner = 0; corner < 3; ++corner) {
        center += findCornerPosition(solidOnly, solidOnly.faces[faces.front()], corner) / 3.0F;
    }
    std::optional<usize> nearest;
    f32 distance = std::numeric_limits<f32>::max();
    for (const RoomMesh& room : sceneMesh->rooms) {
        if (!isRoomShown(readHiddenRooms, room.room)) {
            continue;
        }
        for (const MeshFace& face : room.faces) {
            const glm::vec3 offset = findFaceCenter(room, face) - center;
            if (const f32 squared = glm::dot(offset, offset); squared < distance) {
                distance = squared;
                nearest = room.room;
            }
        }
    }
    return nearest;
}

void MeshEditor::editSurface(const ComponentEdit& edit,
                             const std::vector<SceneCollisionMaterial>& listed) {
    if ((!edit.isDrawn && !edit.isSolid && !edit.collisionMaterial) || !collision || !surface ||
        (!edit.isDrawn && makeSurfaceRows(listed).bound > 0)) {
        return;
    }
    failure.clear();
    auto [meshPicks, solidOnlyPick] = splitPicks(makePicks());
    RoomDraft solidOnly = makeSolidOnlyDraft(*collision, *surface, roomCount);
    std::optional<CollisionMaterial> material;
    std::vector<OperatedRoom> operated;
    if (edit.isDrawn && *edit.isDrawn) {
        if (!solidOnlyPick) {
            return;
        }
        const std::optional<usize> room = findNearestRoom(solidOnly, solidOnlyPick->faces);
        if (!room) {
            failure = "No shown room renders a face to render these as.";
            return;
        }
        operated =
            drawSolidOnlyFaces(*sceneMesh, std::move(solidOnly), solidOnlyPick->faces, *room);
    } else if (edit.isDrawn) {
        operated = undrawFaces(*sceneMesh, meshPicks, std::move(solidOnly));
    } else if (edit.isSolid && *edit.isSolid) {
        operated = makeFacesSolid(*sceneMesh, *collision, meshPicks);
    } else {
        if (edit.collisionMaterial && *edit.collisionMaterial < listed.size()) {
            material = listed[*edit.collisionMaterial].material;
        } else if (!edit.isSolid) {
            return;
        }
        operated = setFacesCollision(*sceneMesh, meshPicks, material);
        if (solidOnlyPick) {
            operated.push_back(
                setSolidOnlyCollision(std::move(solidOnly), solidOnlyPick->faces, material));
        }
    }
    rebuild(operated, std::nullopt);
}

std::vector<CollisionPolygonRef> MeshEditor::listPickedPolygons() const {
    std::vector<CollisionPolygonRef> polygons;
    if (!editMesh || !surface || !collision) {
        return polygons;
    }
    for (const ComponentRef& face : selection.listFaces(*editMesh)) {
        const usize source = findFaceSource(editMesh->pieces[face.piece], face.index);
        const std::optional<usize> polygon =
            face.piece < roomCount ? findFacePolygon(*surface, {.room = face.piece, .face = source})
                                   : std::optional<usize>(source);
        if (polygon && *polygon < collision->polygons.size()) {
            polygons.push_back({.layer = readLayer, .polygon = *polygon});
        }
    }
    return polygons;
}

void MeshEditor::drawMaterialMenu(const usize material) {
    if (material >= materials.size()) {
        return;
    }
    const SceneMaterial& picked = materials[material];
    if (ImGui::IsWindowAppearing()) {
        materialName = picked.name;
    }
    if (ImGui::MenuItem("Select Its Faces")) {
        pickMaterial(material);
    }
    ImGui::SetItemTooltip("Picks every face drawn under it, as double-clicking it does.");
    if (ImGui::MenuItem("Assign to Selected Faces", nullptr, false,
                        canOperate(MeshOperation::Flip))) {
        assignPicked(material);
    }
    drawBindingMenu(material);
    ImGui::Separator();
    const auto drawNamed = [this, &picked](const char* label, const bool isRename) {
        if (!ImGui::BeginMenu(label, picked.isListed())) {
            if (!picked.isListed()) {
                ImGui::SetItemTooltip("Only a material set by a list of its own, as fast64 "
                                      "writes them, has a name to change or copy.");
            }
            return;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * MenuWrapEms / 2.0F);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::InputText("##name", &materialName, ImGuiInputTextFlags_EnterReturnsTrue)) {
            const SceneMaterial copied = picked;
            const std::string name = materialName;
            applyEdit([copied, name, isRename](SceneModel& model) {
                return isRename ? renameMaterial(model, copied, name)
                                : duplicateMaterial(model, copied, name);
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemTooltip("%s", isRename ? "The new name, written into the material's list "
                                               "in every room. Enter renames it."
                                             : "The copy's name. Enter copies the material's "
                                               "list in every room, drawn by no face yet.");
        ImGui::EndMenu();
    };
    drawNamed("Rename", true);
    drawNamed("Duplicate", false);
    ImGui::BeginDisabled();
    ImGui::MenuItem("Edit Combiner");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Materials are edited in a later version.");
}

std::vector<viewport::StripAction> MeshEditor::makeStripActions(const SelectionMode mode) {
    std::vector<viewport::StripAction> actions;
    const auto add = [this, &actions](const char* icon, const char* name,
                                      const MeshOperation operation,
                                      const std::optional<core::Action> action) {
        actions.push_back(viewport::StripAction{
            .icon = icon,
            .name = name,
            .action = action,
            .whyNot =
                canOperate(operation) ? std::string() : std::string(findMissingPick(operation)),
            .run = [this, operation] { operate(operation); }});
    };
    add(core::icons::ArrowFatLineUp, "Extrude Faces", MeshOperation::Extrude,
        core::Action::ExtrudeFaces);
    add(core::icons::FrameCorners, "Inset Faces", MeshOperation::Inset, core::Action::InsetFaces);
    add(core::icons::GridFour, "Subdivide", MeshOperation::Subdivide, core::Action::SubdivideFaces);
    add(core::icons::Swap, "Flip Faces", MeshOperation::Flip, core::Action::FlipFaces);
    add(core::icons::ArrowsSplit, "Split Faces", MeshOperation::Split, core::Action::SplitFaces);
    if (mode != SelectionMode::Face) {
        add(core::icons::ArrowsMerge, "Merge at Center", MeshOperation::Merge,
            core::Action::MergeVertices);
        actions.push_back(
            viewport::StripAction{.icon = core::icons::Triangle,
                                  .name = "Make Face",
                                  .action = core::Action::FillVertices,
                                  .whyNot = selection.listVertices(*editMesh).size() >= 3
                                                ? std::string()
                                                : std::string("Pick three vertices or more."),
                                  .run = [this] { fill(); }});
    }
    add(core::icons::Trash, "Delete Faces", MeshOperation::Delete, core::Action::DeleteComponents);
    const bool canAdd = findShapeRoom() != nullptr;
    for (const auto& [icon, name, shape, action] :
         {std::tuple{core::icons::Cube, "Add Box", MeshShape::Box, core::Action::AddBox},
          std::tuple{core::icons::Square, "Add Plane", MeshShape::Plane, core::Action::AddPlane}}) {
        actions.push_back(viewport::StripAction{
            .icon = icon,
            .name = name,
            .action = action,
            .whyNot =
                canAdd ? std::string() : std::string("No shown room has a face to draw it as."),
            .run = [this, shape] { addShape(shape); }});
    }
    return actions;
}

void MeshEditor::applyHotkeys() {
    if (!viewportPanel.isHovered()) {
        return;
    }
    if (core::isActionPressed(core::Action::DeleteComponents) ||
        core::isActionPressed(core::Action::DeleteComponentsAgain)) {
        operate(MeshOperation::Delete);
    } else if (core::isActionPressed(core::Action::MergeVertices)) {
        operate(MeshOperation::Merge);
    } else if (core::isActionPressed(core::Action::ExtrudeFaces)) {
        operate(MeshOperation::Extrude);
    } else if (core::isActionPressed(core::Action::InsetFaces)) {
        operate(MeshOperation::Inset);
    } else if (core::isActionPressed(core::Action::SubdivideFaces)) {
        operate(MeshOperation::Subdivide);
    } else if (core::isActionPressed(core::Action::FlipFaces)) {
        operate(MeshOperation::Flip);
    } else if (core::isActionPressed(core::Action::SplitFaces)) {
        operate(MeshOperation::Split);
    } else if (core::isActionPressed(core::Action::FillVertices)) {
        fill();
    } else if (core::isActionPressed(core::Action::SelectAll)) {
        selectAll();
    } else if (core::isActionPressed(core::Action::SelectNone)) {
        selection.clear();
        isOfferStale = true;
    } else if (core::isActionPressed(core::Action::InvertSelection)) {
        invertSelection();
    } else if (core::isActionPressed(core::Action::AddBox)) {
        addShape(MeshShape::Box);
    } else if (core::isActionPressed(core::Action::AddPlane)) {
        addShape(MeshShape::Plane);
    }
}

void MeshEditor::drawMenu() {
    if (editMesh) {
        ImGui::TextDisabled("%zu faces", selection.listFaces(*editMesh).size());
    }
    if (!failure.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * MenuWrapEms);
        ImGui::TextColored(core::palette::Danger, "%s", failure.c_str());
        ImGui::PopTextWrapPos();
    }
    const auto drawItem = [this](const char* label, const MeshOperation operation,
                                 const std::optional<core::Action> action, const char* tooltip) {
        const std::string shortcut = action ? core::getShortcutLabel(*action) : std::string();
        if (ImGui::MenuItem(label, shortcut.c_str(), false, canOperate(operation))) {
            operate(operation);
        }
        ImGui::SetItemTooltip("%s", tooltip);
    };
    drawItem("Extrude Faces", MeshOperation::Extrude, core::Action::ExtrudeFaces,
             "Grow the picked faces out of the rest, joined to where they stood by new faces at "
             "their open edges, and move them with the mouse.");
    drawItem("Inset Faces", MeshOperation::Inset, core::Action::InsetFaces,
             "Shrink each picked face toward its middle, joined to where it was by a ring of new "
             "faces textured as it is, and scale it with the mouse.");
    drawItem("Subdivide", MeshOperation::Subdivide, core::Action::SubdivideFaces,
             "Cut each edge of the picked faces in two, and the faces into four. Faces beside "
             "them are cut where they meet a cut edge, so no crack opens.");
    drawAssignMenu();
    drawRoomMenu();
    ImGui::Separator();
    drawItem("Flip Faces", MeshOperation::Flip, core::Action::FlipFaces,
             "Turn the picked faces around, so that they face and are culled the other way.");
    ImGui::Separator();
    drawItem("Merge at Center", MeshOperation::Merge, core::Action::MergeVertices,
             "Put the picked vertices together at their middle, as one vertex. Faces left with "
             "two corners at it are taken out.");
    const std::string fillShortcut = core::getShortcutLabel(core::Action::FillVertices);
    if (ImGui::MenuItem("Make Face", fillShortcut.c_str(), false,
                        editMesh && selection.listVertices(*editMesh).size() >= 3)) {
        fill();
    }
    ImGui::SetItemTooltip("Make faces between the picked vertices of one room: taken around their "
                          "middle and cut into triangles, each at its own corners, facing the way "
                          "a face beside them does, else the camera, and drawn as a face at one "
                          "of them is.");
    drawItem("Split Faces", MeshOperation::Split, core::Action::SplitFaces,
             "Give the picked faces vertices of their own where they meet the rest, so that they "
             "move and are lit apart from it.");
    ImGui::Separator();
    drawItem("Delete Faces", MeshOperation::Delete, core::Action::DeleteComponents,
             "Take out every face that holds anything picked, rendered or solid or both. Lists "
             "that drew them are rebuilt without them. To keep a solid face without rendering "
             "it, turn Render off in Properties instead.");
    ImGui::Separator();
    const RoomMesh* shapeRoom = findShapeRoom();
    for (const auto& [label, shape, action] :
         {std::tuple{"Add Box", MeshShape::Box, core::Action::AddBox},
          std::tuple{"Add Plane", MeshShape::Plane, core::Action::AddPlane}}) {
        const std::string shortcut = core::getShortcutLabel(action);
        if (ImGui::MenuItem(label, shortcut.c_str(), false, shapeRoom != nullptr)) {
            addShape(shape);
        }
        if (shapeRoom != nullptr) {
            ImGui::SetItemTooltip("Add it where the camera looks, %.0f units wide, to room %zu, "
                                  "drawn as the first picked face is, or the room's first face.",
                                  ShapeSize, shapeRoom->room);
        }
    }
}

void MeshEditor::refresh(const Scene& scene, const SceneView& view) {
    shownScene = &scene;
    if (readLayer != view.layer) {
        clearSelectedMaterial();
    }
    if (readImages != scene.images || readLayer != view.layer || !editMesh || !sceneMesh) {
        read(scene, view);
        isMaterialOverlayStale = true;
        if (const auto* render =
                selectedMaterial ? std::get_if<RenderMaterialRef>(&*selectedMaterial) : nullptr;
            render != nullptr && render->material >= materials.size()) {
            clearSelectedMaterial();
        }
    }
    if (readHiddenRooms != view.hiddenRooms || readShowsCollision != view.showsCollision) {
        readHiddenRooms = view.hiddenRooms;
        readShowsCollision = view.showsCollision;
        isOfferStale = true;
        isMaterialOverlayStale = true;
    }
}

void MeshEditor::offer(viewport::ViewportOverlay& overlay) {
    if (!editMesh || !sceneMesh) {
        return;
    }
    const SelectionMode mode = viewportPanel.getSelectionMode();
    if (mode != selection.getMode()) {
        selection.setMode(mode, *editMesh);
        isOfferStale = true;
    }
    if (isOfferStale) {
        makeShown();
        pickedSurfaceType = makePickedSurfaceType();
        makeOffer();
        makeOverlay();
        isOfferStale = false;
    }
    viewportPanel.setPickables(pickables,
                               [this](const viewport::ViewportPanel::ViewportPick& hit,
                                      const viewport::PickMode pickMode) { pick(hit, pickMode); });
    viewportPanel.setSelectedOwners({});
    viewportPanel.setEditMenu("Surface", [this] { drawMenu(); });
    viewportPanel.setStripActions(makeStripActions(mode));
    applyHotkeys();
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
    if (selection.getMode() == SelectionMode::Face) {
        glm::vec3 normals(0.0F);
        for (const ComponentRef& face : selection.listFaces(*editMesh)) {
            const EditPiece& piece = editMesh->pieces[face.piece];
            const std::array<usize, 3>& corners = piece.faces[face.index];
            normals += glm::cross(piece.vertices[corners[1]] - piece.vertices[corners[0]],
                                  piece.vertices[corners[2]] - piece.vertices[corners[0]]);
        }
        if (glm::length(normals) > 0.0F) {
            transformTarget.normal = glm::normalize(normals);
        }
    }
    viewportPanel.setTransformTarget(
        std::move(transformTarget),
        [this](const viewport::TransformEdit& edit) { commit(edit.points); },
        [this](const std::optional<viewport::TransformEdit>& edit) { preview(edit); });
}

MeshEditor::Moves MeshEditor::makeMoves(const std::span<const glm::vec3> points) const {
    Moves moves;
    for (usize i = 0; i < transformed.size() && i < points.size(); ++i) {
        const ComponentRef& vertex = transformed[i];
        if (vertex.piece < roomCount) {
            moves.mesh.push_back(
                {.room = vertex.piece, .vertex = vertex.index, .position = points[i]});
        } else {
            moves.collision.push_back(
                {.vertex = vertex.index, .position = roundToCollision(points[i])});
        }
    }
    if (!surface) {
        return moves;
    }
    for (const MeshVertexMove& welded : findWeldedMeshMoves(*surface, moves.collision)) {
        if (std::ranges::none_of(moves.mesh, [&welded](const MeshVertexMove& move) {
                return move.room == welded.room && move.vertex == welded.vertex;
            })) {
            moves.mesh.push_back(welded);
        }
    }
    return moves;
}

std::vector<SymbolWrite> MeshEditor::makeWrites(const Moves& moves) const {
    if (shownScene == nullptr || !sceneMesh || moves.mesh.empty()) {
        return {};
    }
    return makeMeshMoveWrites(*shownScene, *sceneMesh, moves.mesh);
}

std::optional<Collision> MeshEditor::makeMovedSurface(const Moves& moves) const {
    if (!collision || !surface || !sceneMesh || (moves.mesh.empty() && moves.collision.empty())) {
        return std::nullopt;
    }
    Collision moved =
        makeWeldedCollision(*collision, *surface, *sceneMesh, moves.mesh, moves.collision);
    if (moved.vertices == collision->vertices && moved.polygons == collision->polygons) {
        return std::nullopt;
    }
    return moved;
}

bool MeshEditor::commitFlatSolidFaces(const Moves& moves) {
    if (flatSolidFaces.empty() || !sceneMesh || !collision || !surface || moves.mesh.empty() ||
        !moves.collision.empty()) {
        return false;
    }
    std::map<std::array<MeshEntryKey, 3>, CollisionMaterial> flat;
    for (const FlatSolidFace& face : flatSolidFaces) {
        flat.emplace(face.corners, face.material);
    }
    std::map<usize, OperatedRoom> rooms;
    for (const MeshVertexMove& move : moves.mesh) {
        const RoomMesh* room = findRoomMesh(*sceneMesh, move.room);
        if (room == nullptr || move.vertex >= room->vertices.size()) {
            continue;
        }
        auto [found, isNew] = rooms.try_emplace(move.room);
        if (isNew) {
            found->second.draft = makeRoomDraft(*room);
        }
        found->second.draft.vertices[move.vertex] = {.position = move.position, .isMoved = true};
        found->second.vertices.push_back(move.vertex);
    }
    bool isStoodApart = false;
    for (auto& [index, operated] : rooms) {
        const RoomMesh& room = *findRoomMesh(*sceneMesh, index);
        for (usize face = 0; face < room.faces.size(); ++face) {
            std::array<MeshEntryKey, 3> keys;
            for (usize corner = 0; corner < 3; ++corner) {
                keys[corner] = room.entries[room.faces[face].corners[corner]].key;
            }
            const auto found = flat.find(keys);
            if (found == flat.end()) {
                continue;
            }
            DraftFace& drafted = operated.draft.faces[face];
            drafted.collision = found->second;
            drafted.original.reset();
            std::array<std::array<s16, 3>, 3> corners{};
            for (usize corner = 0; corner < 3; ++corner) {
                corners[corner] =
                    roundToCollision(findCornerPosition(operated.draft, drafted, corner));
            }
            isStoodApart = isStoodApart || !isInLine(corners);
        }
    }
    if (!isStoodApart) {
        return false;
    }
    for (const ComponentRef& face : findMeshFaces(selection.listFaces(*editMesh))) {
        if (const auto found = rooms.find(face.piece); found != rooms.end()) {
            found->second.faces.push_back(face.index);
        }
    }
    std::vector<OperatedRoom> operated;
    for (auto& [index, room] : rooms) {
        operated.push_back(std::move(room));
    }
    const std::optional<Collision> welded = makeMovedSurface(moves);
    rebuild(operated, std::nullopt, std::nullopt, welded ? &*welded : nullptr);
    return true;
}

void MeshEditor::commit(const std::span<const glm::vec3> points) {
    const Moves moves = makeMoves(points);
    if (commitFlatSolidFaces(moves)) {
        return;
    }
    std::vector<SymbolWrite> writes = makeWrites(moves);
    std::optional<Collision> moved = makeMovedSurface(moves);
    if (writes.empty() && !moved) {
        return;
    }
    madeWrites = writes;
    madeMesh.reset();
    if (sceneMesh) {
        madeMesh = makeMovedMesh(*sceneMesh, moves.mesh, writes);
    }
    applyEdit([writes = std::move(writes), moved = std::move(moved),
               layer = readGameLayer](SceneModel& model) -> std::expected<void, base::Diagnostic> {
        if (std::expected<void, base::Diagnostic> applied = applySymbolWrites(model, writes);
            !applied) {
            return applied;
        }
        if (moved) {
            return setCollisionGeometry(model, layer, *moved);
        }
        return {};
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
    showPreview(Preview{.writes = makeWrites(moves), .collision = makeMovedSurface(moves)});
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
        if (const std::optional<ComponentRef> face =
                findHitFace(*editMesh, hit.ray->origin, hit.ray->direction, editShown.faces)) {
            picked.push_back(*face);
        }
    }
    selection.pick(picked, pickMode);
    clearSelectedMaterial();
    isOfferStale = true;
}

void MeshEditor::makeShown() {
    std::vector<std::vector<bool>> faces;
    for (usize piece = 0; piece < editMesh->pieces.size(); ++piece) {
        const EditPiece& edited = editMesh->pieces[piece];
        if (piece >= roomCount || isRoomShown(readHiddenRooms, piece)) {
            faces.emplace_back(edited.faces.size(), piece < roomCount || readShowsCollision);
            continue;
        }
        const RoomMesh* room = findRoomMesh(*sceneMesh, piece);
        std::vector<bool>& isShown = faces.emplace_back(edited.faces.size(), false);
        for (usize face = 0; readShowsCollision && room != nullptr && face < isShown.size();
             ++face) {
            const usize source = findFaceSource(edited, face);
            isShown[face] = source < room->faces.size() && room->faces[source].collision;
        }
    }
    editShown = makeEditShown(*editMesh, std::move(faces));
}

void MeshEditor::makeOffer() {
    pickables = {};
    pickedPoints.clear();
    pickedSegments.clear();
    pickedBoxes.clear();
    const SelectionMode mode = selection.getMode();
    for (usize piece = 0; piece < editMesh->pieces.size(); ++piece) {
        const EditPiece& shown = editMesh->pieces[piece];
        if (mode == SelectionMode::Vertex) {
            for (usize vertex = 0; vertex < shown.vertices.size(); ++vertex) {
                if (!editShown.vertices[piece][vertex]) {
                    continue;
                }
                pickables.points.push_back(shown.vertices[vertex]);
                pickedPoints.push_back({.piece = piece, .index = vertex});
            }
        } else if (mode == SelectionMode::Edge) {
            for (usize edge = 0; edge < shown.edges.size(); ++edge) {
                if (!editShown.edges[piece][edge]) {
                    continue;
                }
                const std::array<usize, 2>& ends = shown.edges[edge].vertices;
                pickables.segments.push_back({shown.vertices[ends[0]], shown.vertices[ends[1]]});
                pickedSegments.push_back({.piece = piece, .index = edge});
            }
        } else {
            for (usize face = 0; face < shown.faces.size(); ++face) {
                if (!editShown.faces[piece][face]) {
                    continue;
                }
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
    const std::vector<ComponentRef> heldFaces = selection.listFaces(*editMesh);
    const std::set<ComponentRef> held(heldFaces.begin(), heldFaces.end());
    for (usize piece = 0; piece < shownMesh->pieces.size(); ++piece) {
        const EditPiece& shown = shownMesh->pieces[piece];
        for (usize edge = 0; edge < shown.edges.size(); ++edge) {
            if (!editShown.edges[piece][edge]) {
                continue;
            }
            const std::array<usize, 2>& ends = shown.edges[edge].vertices;
            const bool isSelected =
                mode == SelectionMode::Edge
                    ? selection.contains({.piece = piece, .index = edge})
                    : mode == SelectionMode::Vertex &&
                          selection.contains({.piece = piece, .index = ends[0]}) &&
                          selection.contains({.piece = piece, .index = ends[1]});
            componentOverlay.edges.push_back({.from = shown.vertices[ends[0]],
                                              .to = shown.vertices[ends[1]],
                                              .color = isSelected ? selectedColor : edgeColor,
                                              .width = isSelected ? SelectedEdgeWidth : EdgeWidth});
        }
        for (usize face = 0; face < shown.faces.size(); ++face) {
            if (!editShown.faces[piece][face] || !held.contains({.piece = piece, .index = face})) {
                continue;
            }
            const std::array<usize, 3>& corners = shown.faces[face];
            componentOverlay.faces.push_back(
                {.corners = {shown.vertices[corners[0]], shown.vertices[corners[1]],
                             shown.vertices[corners[2]]},
                 .color = faceColor});
            if (mode == SelectionMode::Face) {
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
                if (!editShown.vertices[piece][vertex]) {
                    continue;
                }
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
