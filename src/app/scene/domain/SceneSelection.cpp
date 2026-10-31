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
constexpr usize SceneKind = 4;
constexpr usize LightSettingKind = 5;
constexpr usize PathKind = 6;
constexpr usize PathPointKind = 7;
constexpr usize WaterBoxKind = 9;

[[nodiscard]] usize makeKey(const usize kind, const usize layer, const usize room,
                            const usize index) {
    return (kind << KindShift) | ((layer & FieldMask) << LayerShift) |
           ((room & FieldMask) << RoomShift) | (index & IndexMask);
}

} // namespace

usize makeNodeKey(const SceneNode& node) {
    if (const auto* light = std::get_if<LightSettingRef>(&node)) {
        return makeKey(LightSettingKind, light->layer, 0, light->index);
    }
    if (const auto* path = std::get_if<PathRef>(&node)) {
        return makeKey(PathKind, path->layer, path->path, 0);
    }
    if (const auto* point = std::get_if<PathPointRef>(&node)) {
        return makeKey(PathPointKind, point->layer, point->path, point->point);
    }
    if (const auto* box = std::get_if<WaterBoxRef>(&node)) {
        return makeKey(WaterBoxKind, box->layer, 0, box->box);
    }
    if (const auto* scene = std::get_if<SceneRef>(&node)) {
        return (SceneKind << KindShift) | ((scene->layer & FieldMask) << LayerShift);
    }
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
    if (kind == SceneKind) {
        return SceneRef{.layer = layer};
    }
    if (kind == LightSettingKind) {
        return LightSettingRef{.layer = layer, .index = key & IndexMask};
    }
    if (kind == PathKind) {
        return PathRef{.layer = layer, .path = room};
    }
    if (kind == PathPointKind) {
        return PathPointRef{.layer = layer, .path = room, .point = key & IndexMask};
    }
    if (kind == WaterBoxKind) {
        return WaterBoxRef{.layer = layer, .box = key & IndexMask};
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

std::vector<SceneNode> SceneSelection::listMovable() const {
    std::vector<SceneNode> movable;
    for (const SceneNode& node : nodes) {
        if (std::holds_alternative<PlacementRef>(node) ||
            std::holds_alternative<PathPointRef>(node) ||
            std::holds_alternative<WaterBoxRef>(node)) {
            movable.push_back(node);
        }
    }
    return movable;
}

void SceneSelection::removeIf(const std::function<bool(const SceneNode&)>& isGone) {
    std::erase_if(nodes, isGone);
}

} // namespace toki::scene
