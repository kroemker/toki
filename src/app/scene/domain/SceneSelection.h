#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/viewport/domain/ViewportTools.h"

#include <functional>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace toki::scene {

/// A room of a layer, as a node of the scene's tree.
struct RoomRef {
    usize layer = 0;
    usize room = 0;

    friend bool operator==(const RoomRef&, const RoomRef&) = default;
};

/// Something of the scene that can be selected: a room or a placement.
using SceneNode = std::variant<PlacementRef, RoomRef>;

using viewport::PickMode;

/// The nodes selected, in the order they were, the last the active one the
/// Properties panel shows.
class SceneSelection {
public:
    void pick(std::span<const SceneNode> nodes, PickMode mode);

    void select(const SceneNode& node) {
        nodes.assign(1, node);
    }

    void clear() {
        nodes.clear();
    }

    [[nodiscard]] bool contains(const SceneNode& node) const;

    [[nodiscard]] bool isEmpty() const {
        return nodes.empty();
    }

    [[nodiscard]] std::span<const SceneNode> getNodes() const {
        return nodes;
    }

    /// The node picked last, if any.
    [[nodiscard]] const SceneNode* findActive() const {
        return nodes.empty() ? nullptr : &nodes.back();
    }

    /// The placements selected, in order.
    [[nodiscard]] std::vector<PlacementRef> listPlacements() const;

    /// Forgets the nodes `isGone` says are no longer there.
    void removeIf(const std::function<bool(const SceneNode&)>& isGone);

private:
    std::vector<SceneNode> nodes;
};

} // namespace toki::scene
