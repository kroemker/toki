#include "SceneSelection.h"

#include <algorithm>
#include <functional>
#include <span>
#include <vector>

namespace toki::scene {

void SceneSelection::pick(const std::span<const SceneNode> picked, const PickMode mode) {
    if (mode == PickMode::Replace) {
        nodes.assign(picked.begin(), picked.end());
        return;
    }
    for (const SceneNode& node : picked) {
        const auto found = std::ranges::find(nodes, node);
        const bool isThere = found != nodes.end();
        if (isThere && mode != PickMode::Add) {
            nodes.erase(found);
        } else if (!isThere && mode != PickMode::Remove) {
            nodes.push_back(node);
        } else if (isThere) {
            nodes.erase(found);
            nodes.push_back(node);
        }
    }
}

bool SceneSelection::contains(const SceneNode& node) const {
    return std::ranges::find(nodes, node) != nodes.end();
}

std::vector<PlacementRef> SceneSelection::listPlacements() const {
    std::vector<PlacementRef> placements;
    for (const SceneNode& node : nodes) {
        if (const auto* placement = std::get_if<PlacementRef>(&node)) {
            placements.push_back(*placement);
        }
    }
    return placements;
}

void SceneSelection::removeIf(const std::function<bool(const SceneNode&)>& isGone) {
    std::erase_if(nodes, isGone);
}

} // namespace toki::scene
