#include "ScreenPick.h"

#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <optional>
#include <span>

namespace toki::viewport {

std::optional<glm::vec2> findScreenPosition(const glm::mat4& viewProjection, const glm::vec2 size,
                                            const glm::vec3& point) {
    const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0F);
    if (clip.w <= 0.0F) {
        return std::nullopt;
    }
    const glm::vec2 device{clip.x / clip.w, clip.y / clip.w};
    return glm::vec2((device.x + 1.0F) * 0.5F * size.x, (1.0F - device.y) * 0.5F * size.y);
}

std::optional<usize> findPickedPoint(const glm::mat4& viewProjection, const glm::vec2 size,
                                     const glm::vec2 mouse, const std::span<const glm::vec3> points,
                                     const f32 radius) {
    std::optional<usize> picked;
    f32 nearest = radius;
    f32 nearestDepth = 0.0F;
    for (usize i = 0; i < points.size(); ++i) {
        const std::optional<glm::vec2> screen = findScreenPosition(viewProjection, size, points[i]);
        if (!screen) {
            continue;
        }
        const f32 distance = glm::length(*screen - mouse);
        const f32 depth = (viewProjection * glm::vec4(points[i], 1.0F)).w;
        if (distance < nearest || (picked && distance == nearest && depth < nearestDepth)) {
            picked = i;
            nearest = distance;
            nearestDepth = depth;
        }
    }
    return picked;
}

} // namespace toki::viewport
