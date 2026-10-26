#include "ScreenPick.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

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

std::optional<usize> findPickedSegment(const glm::mat4& viewProjection, const glm::vec2 size,
                                       const glm::vec2 mouse,
                                       const std::span<const std::array<glm::vec3, 2>> segments,
                                       const f32 radius) {
    std::optional<usize> picked;
    f32 nearest = radius;
    for (usize i = 0; i < segments.size(); ++i) {
        const std::optional<std::array<glm::vec2, 2>> ends =
            findScreenSegment(viewProjection, size, segments[i][0], segments[i][1]);
        if (!ends) {
            continue;
        }
        const glm::vec2 along = (*ends)[1] - (*ends)[0];
        const f32 length = glm::dot(along, along);
        const f32 t = length > 0.0F
                          ? std::clamp(glm::dot(mouse - (*ends)[0], along) / length, 0.0F, 1.0F)
                          : 0.0F;
        const f32 distance = glm::length((*ends)[0] + (along * t) - mouse);
        if (distance < nearest) {
            picked = i;
            nearest = distance;
        }
    }
    return picked;
}

std::vector<usize> findSegmentsInRect(const glm::mat4& viewProjection, const glm::vec2 size,
                                      const glm::vec2 corner, const glm::vec2 otherCorner,
                                      const std::span<const std::array<glm::vec3, 2>> segments) {
    std::vector<glm::vec3> ends;
    ends.reserve(segments.size() * 2);
    for (const std::array<glm::vec3, 2>& segment : segments) {
        ends.push_back(segment[0]);
        ends.push_back(segment[1]);
    }
    const std::vector<usize> inside =
        findPointsInRect(viewProjection, size, corner, otherCorner, ends);
    std::vector<usize> found;
    for (usize i = 0; i + 1 < inside.size(); ++i) {
        if (inside[i] % 2 == 0 && inside[i + 1] == inside[i] + 1) {
            found.push_back(inside[i] / 2);
            ++i;
        }
    }
    return found;
}

PickRay makePickRay(const glm::mat4& viewProjection, const glm::vec2 size, const glm::vec2 mouse) {
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const glm::vec2 device{(mouse.x / size.x * 2.0F) - 1.0F, 1.0F - (mouse.y / size.y * 2.0F)};
    const glm::vec4 near = inverse * glm::vec4(device, -1.0F, 1.0F);
    const glm::vec4 far = inverse * glm::vec4(device, 1.0F, 1.0F);
    const glm::vec3 origin = glm::vec3(near) / near.w;
    return {.origin = origin, .direction = glm::normalize((glm::vec3(far) / far.w) - origin)};
}

namespace {

/// The clip-space segment from `first` to `second` with what is behind the
/// near plane cut off, as two points on an image of `size`.
[[nodiscard]] std::optional<std::array<glm::vec2, 2>>
projectClipSegment(glm::vec4 first, glm::vec4 second, const glm::vec2 size) {
    constexpr f32 Near = 1.0e-4F;
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
    return std::array{toScreen(first), toScreen(second)};
}

} // namespace

std::optional<std::array<glm::vec2, 2>> findScreenSegment(const glm::mat4& viewProjection,
                                                          const glm::vec2 size,
                                                          const glm::vec3& start,
                                                          const glm::vec3& end) {
    return projectClipSegment(viewProjection * glm::vec4(start, 1.0F),
                              viewProjection * glm::vec4(end, 1.0F), size);
}

std::optional<f32> findAxisDistance(const glm::mat4& viewProjection, const glm::vec2 size,
                                    const glm::vec3& point, const glm::vec3& direction,
                                    const glm::vec2 start, const glm::vec2 mouse) {
    constexpr f32 ShortestUnit = 1.0e-4F;
    const std::optional<glm::vec2> from = findScreenPosition(viewProjection, size, point);
    const std::optional<glm::vec2> to =
        findScreenPosition(viewProjection, size, point + glm::normalize(direction));
    if (!from || !to) {
        return std::nullopt;
    }
    const glm::vec2 unit = *to - *from;
    const f32 squared = glm::dot(unit, unit);
    if (!std::isfinite(squared) || squared < ShortestUnit * ShortestUnit) {
        return std::nullopt;
    }
    return glm::dot(mouse - start, unit) / squared;
}

std::optional<std::array<glm::vec2, 2>> findScreenLine(const glm::mat4& viewProjection,
                                                       const glm::vec2 size, const glm::vec3& point,
                                                       const glm::vec3& direction) {
    constexpr f32 Reach = 1.0e5F;
    const glm::vec3 unit = glm::normalize(direction);
    const std::optional<std::array<glm::vec2, 2>> ends =
        findScreenSegment(viewProjection, size, point - unit * Reach, point + unit * Reach);
    if (!ends) {
        return std::nullopt;
    }
    const glm::vec2 start = (*ends)[0];
    const glm::vec2 along = (*ends)[1] - start;
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
