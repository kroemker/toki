#include "MeshEditor.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/domain/Log.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/ComponentSummary.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MaterialEdits.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
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
constexpr f32 MenuWrapEms = 24.0F;

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
    materials.clear();
    collision.reset();
    editMesh.reset();
    shownMesh.reset();
    madeWrites.clear();
    madeMesh.reset();
    keptMesh.reset();
    wantedFaces.clear();
    wantedVertices.clear();
    wantedTransform.reset();
    failure.clear();
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
            const SceneMesh* kept = nullptr;
            if (keptMesh && keptMesh->layer == view.layer) {
                kept = &*keptMesh;
            } else if (sceneMesh && sceneMesh->layer == view.layer) {
                kept = &*sceneMesh;
            }
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
    keptMesh.reset();
    materials = sceneMesh ? listSceneMaterials(scene, *sceneMesh) : std::vector<SceneMaterial>{};
    if (target == EditTarget::Collision) {
        editMesh = collision ? makeCollisionEdit(*collision) : EditMesh{};
    } else {
        const usize roomCount =
            view.layer < scene.layers.size() ? scene.layers[view.layer].rooms.size() : 0;
        editMesh = makeMeshEdit(*sceneMesh, roomCount);
    }
    shownMesh = editMesh;
    selection.keepWithin(*editMesh);
    pickWanted();
}

void MeshEditor::pickWanted() {
    if (wantedFaces.empty() && wantedVertices.empty()) {
        return;
    }
    if (target == EditTarget::Mesh && sceneMesh) {
        const std::set<std::array<MeshEntryKey, 3>> faces(wantedFaces.begin(), wantedFaces.end());
        const std::set<MeshEntryKey> vertices(wantedVertices.begin(), wantedVertices.end());
        std::vector<ComponentRef> pickedFaces;
        std::vector<ComponentRef> pickedVertices;
        for (const RoomMesh& room : sceneMesh->rooms) {
            for (usize face = 0; face < room.faces.size(); ++face) {
                std::array<MeshEntryKey, 3> keys;
                for (usize corner = 0; corner < 3; ++corner) {
                    keys[corner] = room.entries[room.faces[face].corners[corner]].key;
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
        if (!pickedFaces.empty()) {
            selection.selectFaces(pickedFaces, *editMesh);
        } else {
            selection.selectVertices(pickedVertices, *editMesh);
        }
        isOfferStale = true;
    }
    wantedFaces.clear();
    wantedVertices.clear();
    if (wantedTransform && !selection.isEmpty()) {
        viewportPanel.startTransform(*wantedTransform);
    }
    wantedTransform.reset();
}

std::vector<MeshPick> MeshEditor::makePicks() const {
    std::map<usize, MeshPick> picks;
    const auto add = [&picks](const std::vector<ComponentRef>& refs,
                              std::vector<usize> MeshPick::*list) {
        for (const ComponentRef& ref : refs) {
            MeshPick& pick = picks[ref.piece];
            pick.room = ref.piece;
            (pick.*list).push_back(ref.index);
        }
    };
    add(selection.listFaces(*editMesh), &MeshPick::faces);
    add(selection.listTouchedFaces(*editMesh), &MeshPick::touchedFaces);
    add(selection.listVertices(*editMesh), &MeshPick::vertices);
    std::vector<MeshPick> listed;
    for (auto& [room, pick] : picks) {
        listed.push_back(std::move(pick));
    }
    return listed;
}

bool MeshEditor::canOperate(const MeshOperation operation) const {
    if (target != EditTarget::Mesh || !sceneMesh || !editMesh || shownScene == nullptr ||
        selection.isEmpty()) {
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
    rebuild(operateMesh(*sceneMesh, operation, makePicks()), then);
}

void MeshEditor::rebuild(const std::vector<OperatedRoom>& operated,
                         const std::optional<viewport::TransformKind> then) {
    failure.clear();
    if (operated.empty() || shownScene == nullptr || !sceneMesh) {
        return;
    }
    std::vector<RoomDraft> drafts;
    for (const OperatedRoom& room : operated) {
        drafts.push_back(room.draft);
    }
    std::expected<MeshRebuild, base::Diagnostic> planned =
        planMeshRebuild(*shownScene, SceneModel(*shownScene), *sceneMesh, drafts);
    if (!planned) {
        failure = planned.error().message;
        core::logWarn("{}: {}", shownScene->name, failure);
        return;
    }
    if (planned->isEmpty()) {
        return;
    }
    wantedFaces.clear();
    wantedVertices.clear();
    for (usize draft = 0; draft < operated.size(); ++draft) {
        for (const usize face : operated[draft].faces) {
            wantedFaces.push_back(planned->faces[draft][face]);
        }
        for (const usize vertex : operated[draft].vertices) {
            for (usize face = 0; face < drafts[draft].faces.size(); ++face) {
                for (usize corner = 0; corner < 3; ++corner) {
                    if (drafts[draft].faces[face].corners[corner].vertex == vertex) {
                        wantedVertices.push_back(planned->faces[draft][face][corner]);
                    }
                }
            }
        }
    }
    keptMesh = planned->kept;
    wantedTransform = then;
    applyEdit(
        [made = std::move(*planned)](SceneModel& model) { return applyMeshRebuild(model, made); });
}

void MeshEditor::pickMaterial(const usize material) {
    if (target != EditTarget::Mesh || !sceneMesh || !editMesh) {
        return;
    }
    std::vector<ComponentRef> faces;
    for (const RoomMesh& room : sceneMesh->rooms) {
        for (usize face = 0; face < room.faces.size(); ++face) {
            if (findFaceMaterial(materials, room.faces[face]) == material) {
                faces.push_back({.piece = room.room, .index = face});
            }
        }
    }
    selection.selectFaces(faces, *editMesh);
    isOfferStale = true;
}

void MeshEditor::assignPicked(const usize material) {
    failure.clear();
    if (!canOperate(MeshOperation::Flip) || material >= materials.size()) {
        return;
    }
    const std::expected<std::vector<OperatedRoom>, base::Diagnostic> operated =
        assignMaterial(*sceneMesh, makePicks(), materials[material]);
    if (!operated) {
        failure = operated.error().message;
        core::logWarn("{}: {}", shownScene->name, failure);
        return;
    }
    rebuild(*operated, std::nullopt);
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
        const std::vector<ComponentRef> faces = selection.listFaces(*editMesh);
        if (!faces.empty()) {
            return findRoomMesh(*sceneMesh, faces.front().piece);
        }
    }
    for (const RoomMesh& room : sceneMesh->rooms) {
        if (!room.faces.empty() && isPieceShown(readHiddenRooms, target, room.room)) {
            return &room;
        }
    }
    return nullptr;
}

void MeshEditor::addShape(const MeshShape shape) {
    failure.clear();
    const RoomMesh* room = findShapeRoom();
    if (target != EditTarget::Mesh || room == nullptr || !editMesh) {
        return;
    }
    OperatedRoom made{.draft = makeRoomDraft(*room)};
    usize like = 0;
    for (const ComponentRef& face : selection.listFaces(*editMesh)) {
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
    const ComponentSummary summary =
        summarizeComponents(*sceneMesh, materials, vertices, selection.listFaces(*editMesh));
    const ComponentEdit edit = componentForm.draw(summary, materials, isCollisionLinked);
    if (edit.isLinked) {
        isCollisionLinked = *edit.isLinked;
    }
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
            makeColorWrites(*sceneMesh, vertices, edit.color, edit.alpha);
        if (!writes.empty()) {
            applyEdit([writes = std::move(writes)](SceneModel& model) {
                return applySymbolWrites(model, writes);
            });
        }
    }
}

void MeshEditor::drawMaterialMenu(const usize material) {
    if (material >= materials.size()) {
        return;
    }
    const SceneMaterial& picked = materials[material];
    if (ImGui::IsWindowAppearing()) {
        materialName = picked.name;
    }
    if (ImGui::MenuItem("Select Its Faces", nullptr, false, target == EditTarget::Mesh)) {
        pickMaterial(material);
    }
    if (ImGui::MenuItem("Assign to Selected Faces", nullptr, false,
                        canOperate(MeshOperation::Flip))) {
        assignPicked(material);
    }
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

void MeshEditor::applyHotkeys() {
    if (!viewportPanel.isHovered()) {
        return;
    }
    if (core::isActionPressed(core::Action::DeleteComponents)) {
        operate(MeshOperation::Delete);
    } else if (core::isActionPressed(core::Action::MergeVertices)) {
        operate(MeshOperation::Merge);
    } else if (core::isActionPressed(core::Action::ExtrudeFaces)) {
        operate(MeshOperation::Extrude);
    } else if (core::isActionPressed(core::Action::InsetFaces)) {
        operate(MeshOperation::Inset);
    } else if (core::isActionPressed(core::Action::AddMesh) && target == EditTarget::Mesh) {
        viewportPanel.openEditMenu();
    }
}

void MeshEditor::drawMenu() {
    if (target != EditTarget::Mesh) {
        ImGui::TextDisabled("Faces are edited on the mesh. The collision's are moved.");
        return;
    }
    if (editMesh) {
        ImGui::TextDisabled("Mesh · %zu faces", selection.listFaces(*editMesh).size());
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
    drawItem("Subdivide", MeshOperation::Subdivide, std::nullopt,
             "Cut each edge of the picked faces in two, and the faces into four. Faces beside "
             "them are cut where they meet a cut edge, so no crack opens.");
    drawAssignMenu();
    drawRoomMenu();
    ImGui::Separator();
    drawItem("Flip Faces", MeshOperation::Flip, std::nullopt,
             "Turn the picked faces around, so that they face and are culled the other way.");
    ImGui::Separator();
    drawItem("Merge at Center", MeshOperation::Merge, core::Action::MergeVertices,
             "Put the picked vertices together at their middle, as one vertex. Faces left with "
             "two corners at it are taken out.");
    drawItem("Split Faces", MeshOperation::Split, std::nullopt,
             "Give the picked faces vertices of their own where they meet the rest, so that they "
             "move and are lit apart from it.");
    ImGui::Separator();
    drawItem("Delete Faces", MeshOperation::Delete, core::Action::DeleteComponents,
             "Take out every face that holds anything picked. Lists that drew them are rebuilt "
             "without them.");
    ImGui::Separator();
    const RoomMesh* shapeRoom = findShapeRoom();
    const std::string shortcut = core::getShortcutLabel(core::Action::AddMesh);
    for (const auto& [label, shape] :
         {std::pair{"Add Box", MeshShape::Box}, std::pair{"Add Plane", MeshShape::Plane}}) {
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
    viewportPanel.setEditMenu("Mesh", [this] { drawMenu(); });
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
