#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/MeshShapes.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneView.h"
#include "app/scene/ui/ComponentForm.h"
#include "app/viewport/domain/TransformSession.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/viewport/ui/ViewportTransform.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

/// The faces, edges and vertices of the shown layer's rooms or of its
/// collision, offered to the viewport to pick and transform while it
/// selects by them.
class MeshEditor {
public:
    /// What an edit of the mesh writes, and the collision it leaves, shown
    /// before it is made.
    struct Preview {
        std::vector<SymbolWrite> writes;
        std::optional<Collision> collision;
    };

    /// What makes an edit of the scene, as one step.
    using ApplyEdit = std::function<void(SceneEditor::ModelEdit)>;
    /// What shows an edit before it is made, or, given nothing, that it ended.
    using ShowPreview = std::function<void(const std::optional<Preview>&)>;

    MeshEditor(viewport::ViewportPanel& viewportPanel, ApplyEdit applyEdit, ShowPreview showPreview)
        : viewportPanel(viewportPanel), applyEdit(std::move(applyEdit)),
          showPreview(std::move(showPreview)) {}

    [[nodiscard]] EditTarget getTarget() const {
        return target;
    }

    /// Picks by faces, edges and vertices of `next` from now on, what was
    /// picked of the other let go.
    void setTarget(EditTarget next);

    /// Whether the collision's vertices follow the mesh's where they stand
    /// at one place, and the mesh's the collision's.
    [[nodiscard]] bool isLinked() const {
        return isCollisionLinked;
    }

    void setLinked(const bool isOn) {
        isCollisionLinked = isOn;
    }

    /// Offers the viewport what it picks in the mode it selects by, and what
    /// is picked to transform, and adds to `overlay` the edges, vertices and
    /// selection drawn over it. Called every frame while the viewport selects
    /// by faces, edges or vertices; what is edited is read again whenever
    /// the scene's images or the shown layer changed.
    void offer(const Scene& scene, const SceneView& view, viewport::ViewportOverlay& overlay);

    /// Forgets the scene, when another is shown.
    void reset();

    /// Draws the items of the viewport's Mesh menu: what is done to what is
    /// picked.
    void drawMenu();

    /// The materials the rooms are drawn with, once the mesh was read.
    [[nodiscard]] const std::vector<SceneMaterial>* findMaterials() const {
        return sceneMesh ? &materials : nullptr;
    }

    /// Selects the faces drawn under the material numbered `material`.
    void pickMaterial(usize material);

    /// Draws the items of a material's context menu.
    void drawMaterialMenu(usize material);

    /// Whether faces or vertices of the rooms' meshes are picked, which the
    /// Properties panel shows in place of what the Scene panel selects.
    [[nodiscard]] bool hasPicked() const {
        return target == EditTarget::Mesh && sceneMesh && editMesh && !selection.isEmpty();
    }

    /// Draws the Properties panel's form of what is picked.
    void drawProperties();

private:
    /// The moves a transform of the selection to `points` makes, of the
    /// target and of what is linked with it.
    struct Moves {
        std::vector<MeshVertexMove> mesh;
        std::vector<CollisionVertexMove> collision;
    };

    void read(const Scene& scene, const SceneView& view);
    void pick(const viewport::ViewportPanel::ViewportPick& hit, viewport::PickMode mode);
    void offerTransform();
    [[nodiscard]] Moves makeMoves(std::span<const glm::vec3> points) const;
    [[nodiscard]] std::vector<SymbolWrite> makeWrites(const Moves& moves) const;
    void commit(std::span<const glm::vec3> points);
    void preview(const std::optional<viewport::TransformEdit>& edit);
    void makeOffer();
    void makeOverlay();
    void applyHotkeys();
    [[nodiscard]] bool canOperate(MeshOperation operation) const;
    void operate(MeshOperation operation);
    /// Writes what `operated` made of rooms, and picks what it says once
    /// the scene is read again, then begins a transform of it of `then`.
    void rebuild(const std::vector<OperatedRoom>& operated,
                 std::optional<viewport::TransformKind> then);
    [[nodiscard]] std::vector<MeshPick> makePicks() const;
    /// Picks what the last rebuild asked for, in the mesh read after it.
    void pickWanted();
    void assignPicked(usize material);
    void drawAssignMenu();
    void addShape(MeshShape shape);
    void moveToRoom(usize room);
    void drawRoomMenu();
    /// The room a shape is added to: that of the faces picked, else the
    /// first shown room that has faces.
    [[nodiscard]] const RoomMesh* findShapeRoom() const;

    viewport::ViewportPanel& viewportPanel;
    ApplyEdit applyEdit;
    ShowPreview showPreview;
    EditTarget target = EditTarget::Mesh;
    bool isCollisionLinked = true;
    /// The scene offered last, whose edits the viewport's handlers make.
    const Scene* shownScene = nullptr;
    /// The images what is edited was read from, to tell when to read it again.
    std::vector<std::shared_ptr<const z64::MemoryImage>> readImages;
    usize readLayer = 0;
    std::vector<bool> readHiddenRooms;
    std::optional<SceneMesh> sceneMesh;
    std::vector<SceneMaterial> materials;
    /// What a material is renamed or duplicated as, while its menu is open.
    std::string materialName;
    ComponentForm componentForm;
    std::optional<Collision> collision;
    std::optional<EditMesh> editMesh;
    /// `editMesh` as a transform being previewed moves it.
    std::optional<EditMesh> shownMesh;
    /// The vertices the transform offered moves, in the order of its points.
    std::vector<ComponentRef> transformed;
    /// What the last edit made wrote, and the mesh it left, taken as read once
    /// the scene's images hold what it wrote.
    std::vector<SymbolWrite> madeWrites;
    std::optional<SceneMesh> madeMesh;
    /// What the last rebuild grouped its entries into, to read them so.
    std::optional<SceneMesh> keptMesh;
    /// What to pick once the scene is read after the last rebuild: faces by
    /// the entries their corners load from, and vertices by one entry each.
    std::vector<std::array<MeshEntryKey, 3>> wantedFaces;
    std::vector<MeshEntryKey> wantedVertices;
    std::optional<viewport::TransformKind> wantedTransform;
    std::string failure;
    ComponentSelection selection;
    viewport::ViewportPanel::Pickables pickables;
    /// What each of `pickables` stands for, in the same order.
    std::vector<ComponentRef> pickedPoints;
    std::vector<ComponentRef> pickedSegments;
    std::vector<ComponentRef> pickedBoxes;
    viewport::ViewportOverlay componentOverlay;
    bool isOfferStale = true;
};

} // namespace toki::scene
