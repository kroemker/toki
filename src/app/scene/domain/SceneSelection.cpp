#include "SceneSelection.h"

#include <algorithm>
#include <functional>
#include <span>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize KindShift = 56;
constexpr usize LayerShift = 40;
constexpr usize RoomShift = 24;
constexpr usize FieldMask = 0xFFFF;
constexpr usize IndexMask = 0xFF'FFFF;
constexpr usize RoomKind = 3;

} // namespace

usize makeNodeKey(const SceneNode& node) {
    if (const auto* room = std::get_if<RoomRef>(&node)) {
        return (RoomKind << KindShift) | ((room->layer & FieldMask) << LayerShift) |
               ((room->room & FieldMask) << RoomShift);
    }
    const auto& placement = std::get<PlacementRef>(node);
    return (static_cast<usize>(placement.kind) << KindShift) |
           ((placement.layer & FieldMask) << LayerShift) |
           ((placement.room & FieldMask) << RoomShift) | (placement.index & IndexMask);
}

std::optional<SceneNode> findNode(const usize key) {
    const usize kind = key >> KindShift;
    const usize layer = (key >> LayerShift) & FieldMask;
    const usize room = (key >> RoomShift) & FieldMask;
    if (kind == RoomKind) {
        return RoomRef{.layer = layer, .room = room};
    }
    if (kind > static_cast<usize>(PlacementKind::PlayerEntry)) {
        return std::nullopt;
    }
    return PlacementRef{.kind = static_cast<PlacementKind>(kind),
                        .layer = layer,
                        .room = room,
                        .index = key & IndexMask};
}

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
