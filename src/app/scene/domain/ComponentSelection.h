#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/EditMesh.h"
#include "app/viewport/domain/ViewportTools.h"

#include <set>
#include <span>
#include <vector>

namespace toki::scene {

/// The faces, edges or vertices of an `EditMesh` selected, which of them
/// as the selection mode says, kept apart from the nodes selected.
class ComponentSelection {
public:
    [[nodiscard]] viewport::SelectionMode getMode() const {
        return mode;
    }

    /// Selects by `next` from now on, what was selected carried over as
    /// Blender carries it: going to vertices, the vertices of what was
    /// selected; going from vertices or edges to more, what they wholly hold.
    void setMode(viewport::SelectionMode next, const EditMesh& mesh);

    void pick(std::span<const ComponentRef> picked, viewport::PickMode pickMode);

    void clear() {
        components.clear();
    }

    [[nodiscard]] bool contains(const ComponentRef& component) const {
        return components.contains(component);
    }

    [[nodiscard]] bool isEmpty() const {
        return components.empty();
    }

    [[nodiscard]] const std::set<ComponentRef>& getComponents() const {
        return components;
    }

    /// The vertices of what is selected: what a transform moves.
    [[nodiscard]] std::vector<ComponentRef> listVertices(const EditMesh& mesh) const;

    /// The faces what is selected wholly holds: what an edit of faces
    /// changes.
    [[nodiscard]] std::vector<ComponentRef> listFaces(const EditMesh& mesh) const;

    /// The faces that hold anything selected: what deleting it takes out.
    [[nodiscard]] std::vector<ComponentRef> listTouchedFaces(const EditMesh& mesh) const;

    /// Selects `faces` alone, as the mode selects them: themselves, their
    /// vertices, or the edges those hold.
    void selectFaces(std::span<const ComponentRef> faces, const EditMesh& mesh);

    /// Selects `vertices` alone, as the mode selects them: themselves, the
    /// edges they hold, or the faces they hold.
    void selectVertices(std::span<const ComponentRef> vertices, const EditMesh& mesh);

    /// Forgets what `mesh` no longer holds.
    void keepWithin(const EditMesh& mesh);

private:
    viewport::SelectionMode mode = viewport::SelectionMode::Face;
    std::set<ComponentRef> components;
};

} // namespace toki::scene
