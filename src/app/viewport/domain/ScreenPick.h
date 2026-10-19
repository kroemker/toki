#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace toki::viewport {

/// Where `point` falls on an image of `size` that `viewProjection` draws, from
/// its top left, y down, as the mouse is; nothing when it is behind the
/// camera.
[[nodiscard]] std::optional<glm::vec2> findScreenPosition(const glm::mat4& viewProjection,
                                                          glm::vec2 size, const glm::vec3& point);

/// The index of the point of `points` that falls nearest `mouse` on the image,
/// if one falls within `radius` of it; of two as near, the one nearer the
/// camera.
[[nodiscard]] std::optional<usize> findPickedPoint(const glm::mat4& viewProjection, glm::vec2 size,
                                                   glm::vec2 mouse,
                                                   std::span<const glm::vec3> points, f32 radius);

/// The part of the endless line through `point` along `direction` that is on
/// an image of `size` that `viewProjection` draws, from one edge to the other,
/// as two points on the image; nothing when none of it is in front of the
/// camera or on the image.
[[nodiscard]] std::optional<std::array<glm::vec2, 2>>
findScreenLine(const glm::mat4& viewProjection, glm::vec2 size, const glm::vec3& point,
               const glm::vec3& direction);

/// The segment from `start` to `end` as it falls on an image of `size` that
/// `viewProjection` draws, cut where it passes behind the camera; nothing
/// when all of it is behind.
[[nodiscard]] std::optional<std::array<glm::vec2, 2>>
findScreenSegment(const glm::mat4& viewProjection, glm::vec2 size, const glm::vec3& start,
                  const glm::vec3& end);

/// The indices of the points of `points` in front of the camera that fall in
/// the rectangle between `corner` and `otherCorner` on the image, in order.
[[nodiscard]] std::vector<usize> findPointsInRect(const glm::mat4& viewProjection, glm::vec2 size,
                                                  glm::vec2 corner, glm::vec2 otherCorner,
                                                  std::span<const glm::vec3> points);

} // namespace toki::viewport
