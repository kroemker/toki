#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneSelection.h"

#include <glm/ext/vector_float3.hpp>

#include <optional>
#include <span>

namespace toki::scene {

struct Scene;

/// The box around what nodes stand at, in the world. A box of one point is
/// what a placement or a path point gives.
struct NodeBounds {
    glm::vec3 min{0.0F};
    glm::vec3 max{0.0F};

    friend bool operator==(const NodeBounds&, const NodeBounds&) = default;
};

/// The box around every one of `nodes` that stands somewhere: a placement or
/// a path point at its position, a path at its points, a polygon at its
/// corners and a waterbox at its surface. Nothing when none of them does, as
/// a light setting, a room or the scene does not.
[[nodiscard]] std::optional<NodeBounds> findNodeBounds(const Scene& scene,
                                                       std::span<const SceneNode> nodes);

} // namespace toki::scene
