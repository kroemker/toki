#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/ComponentSelection.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "app/viewport/ui/ViewportPanel.h"

#include <memory>
#include <optional>
#include <vector>

namespace toki::scene {

/// The faces, edges and vertices of the shown layer's rooms or of its
/// collision, offered to the viewport to pick while it selects by them.
class MeshEditor {
public:
    explicit MeshEditor(viewport::ViewportPanel& viewportPanel) : viewportPanel(viewportPanel) {}

    [[nodiscard]] EditTarget getTarget() const {
        return target;
    }

    /// Picks by faces, edges and vertices of `next` from now on, what was
    /// picked of the other let go.
    void setTarget(EditTarget next);

    /// Offers the viewport what it picks in the mode it selects by, and
    /// adds to `overlay` the edges, vertices and selection drawn over it.
    /// Called every frame while the viewport selects by faces, edges or
    /// vertices; the mesh is read again whenever the scene's images or the
    /// shown layer changed.
    void offer(const Scene& scene, const SceneView& view, viewport::ViewportOverlay& overlay);

    /// Forgets the scene, when another is shown.
    void reset();

private:
    void read(const Scene& scene, const SceneView& view);
    void pick(const viewport::ViewportPanel::ViewportPick& hit, viewport::PickMode mode);
    void makeOffer();
    void makeOverlay();

    viewport::ViewportPanel& viewportPanel;
    EditTarget target = EditTarget::Mesh;
    /// The images the mesh was read from, to tell when to read it again.
    std::vector<std::shared_ptr<const z64::MemoryImage>> readImages;
    usize readLayer = 0;
    std::vector<bool> readHiddenRooms;
    std::optional<SceneMesh> sceneMesh;
    std::optional<EditMesh> editMesh;
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
