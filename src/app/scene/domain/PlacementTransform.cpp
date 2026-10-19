#include "PlacementTransform.h"

#include "app/scene/domain/BinaryAngle.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <variant>

namespace toki::scene {

namespace {

[[nodiscard]] s16 roundToS16(const f32 value) {
    return static_cast<s16>(std::clamp(std::lround(value), long{INT16_MIN}, long{INT16_MAX}));
}

} // namespace

Placement makeTransformedPlacement(const Placement& placement, const glm::vec3& point,
                                   const viewport::TransformResult& result) {
    Placement transformed = placement;
    const std::array<s16, 3> position{roundToS16(point.x), roundToS16(point.y),
                                      roundToS16(point.z)};
    const bool isTurned = result.kind == viewport::TransformKind::Rotate;
    if (auto* actor = std::get_if<ActorPlacement>(&transformed)) {
        actor->position = position;
        if (isTurned) {
            actor->rotation = rotateBinaryAngles(actor->rotation, result.axis, result.degrees);
        }
    } else if (auto* transition = std::get_if<TransitionActorPlacement>(&transformed)) {
        transition->position = position;
        if (isTurned && result.axis == 1) {
            transition->rotationY =
                rotateBinaryAngles({0, transition->rotationY, 0}, 1, result.degrees)[1];
        }
    }
    return transformed;
}

bool setLayerPlacement(SceneLayer& layer, const PlacementRef& where, const Placement& placement) {
    switch (where.kind) {
    case PlacementKind::Actor:
        if (where.room < layer.rooms.size() &&
            where.index < layer.rooms[where.room].actors.size() &&
            std::holds_alternative<ActorPlacement>(placement)) {
            layer.rooms[where.room].actors[where.index] = std::get<ActorPlacement>(placement);
            return true;
        }
        break;
    case PlacementKind::PlayerEntry:
        if (where.index < layer.header.playerEntries.size() &&
            std::holds_alternative<ActorPlacement>(placement)) {
            layer.header.playerEntries[where.index] = std::get<ActorPlacement>(placement);
            return true;
        }
        break;
    case PlacementKind::TransitionActor:
        if (where.index < layer.header.transitionActors.size() &&
            std::holds_alternative<TransitionActorPlacement>(placement)) {
            layer.header.transitionActors[where.index] =
                std::get<TransitionActorPlacement>(placement);
            return true;
        }
        break;
    }
    return false;
}

} // namespace toki::scene
