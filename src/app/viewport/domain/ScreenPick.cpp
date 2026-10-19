#include "ScreenPick.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

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

std::vector<usize> findPointsInRect(const glm::mat4& viewProjection, const glm::vec2 size,
                                    const glm::vec2 corner, const glm::vec2 otherCorner,
                                    const std::span<const glm::vec3> points) {
    const glm::vec2 lowest = glm::min(corner, otherCorner);
    const glm::vec2 highest = glm::max(corner, otherCorner);
    std::vector<usize> found;
    for (usize i = 0; i < points.size(); ++i) {
        const std::optional<glm::vec2> position =
            findScreenPosition(viewProjection, size, points[i]);
        if (position && position->x >= lowest.x && position->y >= lowest.y &&
            position->x <= highest.x && position->y <= highest.y) {
            found.push_back(i);
        }
    }
    return found;
}

std::optional<std::array<glm::vec2, 2>> findScreenLine(const glm::mat4& viewProjection,
                                                       const glm::vec2 size, const glm::vec3& point,
                                                       const glm::vec3& direction) {
    constexpr f32 Reach = 1.0e5F;
    constexpr f32 Near = 1.0e-4F;
    const glm::vec3 unit = glm::normalize(direction);
    glm::vec4 first = viewProjection * glm::vec4(point - unit * Reach, 1.0F);
    glm::vec4 second = viewProjection * glm::vec4(point + unit * Reach, 1.0F);
    const f32 firstDepth = first.z + first.w;
    const f32 secondDepth = second.z + second.w;
    if (firstDepth < Near && secondDepth < Near) {
        return std::nullopt;
    }
    if (firstDepth < Near) {
        first += (second - first) * ((Near - firstDepth) / (secondDepth - firstDepth));
    } else if (secondDepth < Near) {
        second += (first - second) * ((Near - secondDepth) / (firstDepth - secondDepth));
    }
    const auto toScreen = [size](const glm::vec4& clip) {
        return glm::vec2((clip.x / clip.w + 1.0F) * 0.5F * size.x,
                         (1.0F - clip.y / clip.w) * 0.5F * size.y);
    };
    const glm::vec2 start = toScreen(first);
    const glm::vec2 along = toScreen(second) - start;
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(along.x) ||
        !std::isfinite(along.y) || glm::length(along) < 1.0F) {
        return std::nullopt;
    }
    f32 lowest = 0.0F;
    f32 highest = 1.0F;
    for (glm::length_t axis = 0; axis < 2; ++axis) {
        if (along[axis] == 0.0F) {
            if (start[axis] < 0.0F || start[axis] > size[axis]) {
                return std::nullopt;
            }
            continue;
        }
        f32 enter = -start[axis] / along[axis];
        f32 leave = (size[axis] - start[axis]) / along[axis];
        if (enter > leave) {
            std::swap(enter, leave);
        }
        lowest = std::max(lowest, enter);
        highest = std::min(highest, leave);
    }
    if (lowest > highest) {
        return std::nullopt;
    }
    return std::array{start + along * lowest, start + along * highest};
}

} // namespace toki::viewport
