#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MaterialBindings.h"
#include "app/scene/domain/MaterialRef.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/MeshShapes.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/scene/domain/SceneSurface.h"
#include "app/scene/domain/SceneView.h"
#include "app/scene/domain/SurfaceOperations.h"
#include "app/scene/ui/ComponentForm.h"
#include "app/scene/ui/MaterialForm.h"
#include "app/viewport/domain/TransformSession.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "app/viewport/ui/ToolStrip.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/viewport/ui/ViewportTransform.h"

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

/// The faces, edges and vertices of the shown layer's surface, offered to the
/// viewport to pick and transform while it selects by them: the faces of its
/// shown rooms, and the polygons no face has while its collision is shown.
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

    /// Reads the shown layer's surface again whenever the scene's images or
    /// the layer changed, and keeps up with what `view` shows. Called every
    /// frame a scene is shown, whatever the viewport selects by.
    void refresh(const Scene& scene, const SceneView& view);

    /// Offers the viewport what it picks in the mode it selects by, and what
    /// is picked to transform, and adds to `overlay` the edges, vertices and
    /// selection drawn over it. Called every frame, after `refresh`, while the
    /// viewport selects by faces, edges or vertices.
    void offer(viewport::ViewportOverlay& overlay);

    /// Forgets the scene, when another is shown.
    void reset();

    /// Draws the items of the viewport's Mesh menu: what is done to what is
    /// picked.
    void drawMenu();

    /// The materials the rooms are drawn with, once the mesh was read.
    [[nodiscard]] const std::vector<SceneMaterial>* findMaterials() const {
        return sceneMesh ? &materials : nullptr;
    }

    /// Selects by faces, and selects the faces drawn under the material
    /// numbered `material`.
    void pickMaterial(usize material);

    /// Selects `material`, whose settings the Properties panel then shows and
    /// whose faces the viewport highlights, picking none of them.
    void selectMaterial(const MaterialRef& material);

    [[nodiscard]] const std::optional<MaterialRef>& findSelectedMaterial() const {
        return selectedMaterial;
    }

    void clearSelectedMaterial();

    /// Draws the Properties panel's settings of the render material numbered
    /// `material`.
    void drawMaterialSettings(usize material);

    /// Adds to `overlay` the faces of the material selected. Called every
    /// frame, after `refresh`, whatever the viewport selects by.
    void addMaterialOverlay(viewport::ViewportOverlay& overlay);

    /// Selects by faces, and selects the faces made of `material`, and the
    /// polygons no face has that are.
    void pickCollisionMaterial(const CollisionMaterial& material);

    /// What the material numbered `material` is bound to, named, if it is.
    [[nodiscard]] std::optional<std::string> findBindingName(usize material) const;

    /// The surface type every face picked is made of, where they share one.
    [[nodiscard]] std::optional<u16> findPickedSurfaceType() const {
        return pickedSurfaceType;
    }

    /// Draws the items of a material's context menu.
    void drawMaterialMenu(usize material);

    /// Whether faces or vertices of the rooms' meshes are picked, which the
    /// Properties panel shows in place of what the Scene panel selects.
    [[nodiscard]] bool hasPicked() const {
        return sceneMesh && editMesh && !selection.isEmpty();
    }

    /// Draws the Properties panel's form of what is picked.
    void drawProperties();

    /// The polygons of what is picked: of the solid faces, and those no face
    /// has, for the Properties panel's form of polygons.
    [[nodiscard]] std::vector<CollisionPolygonRef> listPickedPolygons() const;

    /// Whether a face picked is drawn under a material bound to collision.
    [[nodiscard]] bool hasPickedBound() const;

    /// Names collision materials as `names` does.
    void setCollisionNames(const CollisionNames* names) {
        collisionNames = names;
    }

private:
    /// The moves a transform of the selection to `points` makes, of the
    /// vertices picked and of those welded to them.
    struct Moves {
        std::vector<MeshVertexMove> mesh;
        std::vector<CollisionVertexMove> collision;
    };

    /// A face a rebuild made solid with its corners in a line, which has no
    /// polygon until a move stands them apart: by the entries its corners
    /// load from.
    struct FlatSolidFace {
        std::array<MeshEntryKey, 3> corners;
        CollisionMaterial material;
    };

    void read(const Scene& scene, const SceneView& view);
    void pick(const viewport::ViewportPanel::ViewportPick& hit, viewport::PickMode mode);
    void offerTransform();
    [[nodiscard]] Moves makeMoves(std::span<const glm::vec3> points) const;
    [[nodiscard]] std::vector<SymbolWrite> makeWrites(const Moves& moves) const;
    /// The collision `moves` leave, if they change it.
    [[nodiscard]] std::optional<Collision> makeMovedSurface(const Moves& moves) const;
    /// The picks of rooms' meshes `picks` holds, and that of the polygons no
    /// face has.
    [[nodiscard]] std::pair<std::vector<MeshPick>, std::optional<MeshPick>>
    splitPicks(std::vector<MeshPick> picks) const;
    /// What is picked as the rooms' meshes number it: each face of a piece
    /// that holds only the solid faces as its face of the room.
    [[nodiscard]] std::vector<ComponentRef>
    findMeshFaces(std::span<const ComponentRef> faces) const;
    /// Makes `collision` the collision, with `rebuild`, as one step.
    void applySurface(std::optional<MeshRebuild> rebuild, std::optional<Collision> made,
                      std::optional<MaterialBindings> bindings = std::nullopt);
    /// The Surface rows of the Properties panel for what is picked.
    [[nodiscard]] SurfaceRows
    makeSurfaceRows(const std::vector<SceneCollisionMaterial>& listed) const;
    /// Makes what is picked drawn, solid or of a collision material as `edit`
    /// says, `listed` the materials it names by index.
    /// The shown room drawing the face nearest the first of `faces` of
    /// `solidOnly`, which renders them when they are rendered.
    [[nodiscard]] std::optional<usize> findNearestRoom(const RoomDraft& solidOnly,
                                                       std::span<const usize> faces) const;
    void editSurface(const ComponentEdit& edit, const std::vector<SceneCollisionMaterial>& listed);
    void commit(std::span<const glm::vec3> points);
    /// Makes `moves` as a rebuild of the rooms they move, when they stand
    /// any of `flatSolidFaces` apart, so that those are given their polygons.
    [[nodiscard]] bool commitFlatSolidFaces(const Moves& moves);
    void preview(const std::optional<viewport::TransformEdit>& edit);
    /// What is offered of `editMesh`: the faces of the shown rooms, and while
    /// the collision is shown, the solid faces of hidden rooms and the
    /// polygons no face has.
    void makeShown();
    /// Selects by faces, and selects `faces`.
    void selectFaces(const std::vector<ComponentRef>& faces);
    [[nodiscard]] std::vector<ComponentRef> listMaterialFaces(usize material) const;
    /// The faces made of `material`, and the polygons no face has that are.
    [[nodiscard]] std::vector<ComponentRef>
    listCollisionMaterialFaces(const CollisionMaterial& material) const;
    [[nodiscard]] BindingRows makeBindingRows(usize material) const;
    void makeMaterialOverlay();
    [[nodiscard]] std::optional<u16> makePickedSurfaceType() const;
    void makeOffer();
    void makeOverlay();
    void applyHotkeys();
    [[nodiscard]] std::vector<viewport::StripAction> makeStripActions(viewport::SelectionMode mode);
    [[nodiscard]] bool canOperate(MeshOperation operation) const;
    void operate(MeshOperation operation);
    /// Writes what `operated` made of rooms, and picks what it says once
    /// the scene is read again, then begins a transform of it of `then`. The
    /// collision is drafted from `base` where given, else from `collision`.
    void rebuild(const std::vector<OperatedRoom>& operated,
                 std::optional<viewport::TransformKind> then,
                 std::optional<MaterialBindings> bindings = std::nullopt,
                 const Collision* base = nullptr);
    /// Binds the material numbered `material` to `binding`, or unbinds it,
    /// and makes its faces what the binding says.
    void bindMaterial(usize material, std::optional<MaterialBinding> binding);
    void drawBindingMenu(usize material);
    [[nodiscard]] std::vector<MeshPick> makePicks() const;
    /// Picks what the last rebuild asked for, in the mesh read after it.
    void pickWanted();
    void assignPicked(usize material);
    void drawAssignMenu();
    void addShape(MeshShape shape);
    /// Makes faces between the vertices picked, of one room.
    void fill();
    /// What the viewport offers to pick in the mode it selects by.
    [[nodiscard]] const std::vector<ComponentRef>& listOffered() const;
    void selectAll();
    void invertSelection();
    void moveToRoom(usize room);
    void drawRoomMenu();
    /// The room a shape is added to: that of the faces picked, else the
    /// first shown room that has faces.
    [[nodiscard]] const RoomMesh* findShapeRoom() const;

    viewport::ViewportPanel& viewportPanel;
    ApplyEdit applyEdit;
    ShowPreview showPreview;
    /// The scene offered last, whose edits the viewport's handlers make.
    const Scene* shownScene = nullptr;
    /// The images what is edited was read from, to tell when to read it again.
    std::vector<std::shared_ptr<const z64::MemoryImage>> readImages;
    usize readLayer = 0;
    /// The game's scene layer of `readLayer`, which collision edits name.
    usize readGameLayer = 0;
    usize roomCount = 0;
    std::vector<bool> readHiddenRooms;
    bool readShowsCollision = false;
    std::optional<SceneMesh> sceneMesh;
    std::vector<SceneMaterial> materials;
    /// The binding of each of `materials`, where it has one.
    std::vector<std::optional<ResolvedBinding>> resolvedBindings;
    /// What a material is renamed or duplicated as, while its menu is open.
    std::string materialName;
    ComponentForm componentForm;
    const CollisionNames* collisionNames = nullptr;
    std::optional<Collision> collision;
    std::optional<SceneSurface> surface;
    std::optional<EditMesh> editMesh;
    EditShown editShown;
    std::optional<u16> pickedSurfaceType;
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
    /// The meshes read before `sceneMesh`, newest first, so that an undo
    /// reads a room grouped as it was before.
    std::deque<SceneMesh> pastMeshes;
    /// What to pick once the scene is read after the last rebuild: faces by
    /// the entries their corners load from, and vertices by one entry each.
    std::vector<std::array<MeshEntryKey, 3>> wantedFaces;
    std::vector<MeshEntryKey> wantedVertices;
    /// Polygons no face has to pick, by where their corners stand.
    std::vector<std::array<std::array<s16, 3>, 3>> wantedPolygons;
    std::optional<viewport::TransformKind> wantedTransform;
    std::vector<FlatSolidFace> flatSolidFaces;
    std::string failure;
    ComponentSelection selection;
    viewport::ViewportPanel::Pickables pickables;
    /// What each of `pickables` stands for, in the same order.
    std::vector<ComponentRef> pickedPoints;
    std::vector<ComponentRef> pickedSegments;
    std::vector<ComponentRef> pickedBoxes;
    viewport::ViewportOverlay componentOverlay;
    bool isOfferStale = true;
    std::optional<MaterialRef> selectedMaterial;
    viewport::ViewportOverlay materialOverlay;
    bool isMaterialOverlayStale = true;
};

} // namespace toki::scene
