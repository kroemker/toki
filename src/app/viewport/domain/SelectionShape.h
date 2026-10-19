#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/RspResult.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace toki::viewport {

/// What a frame drew, by draw: the outputs each draw made and what owns it,
/// both by the draw's index in the content.
struct DrawnOwners {
    std::span<const std::optional<OutputRange>> ranges;
    std::span<const std::optional<usize>> owners;
};

/// The triangles `result` drew for draws owned by any of `selected`.
[[nodiscard]] std::vector<ClipTriangle> listOwnedTriangles(const n64::RspResult& result,
                                                           const DrawnOwners& drawn,
                                                           std::span<const usize> selected);

/// A box in the world, its lowest and highest corner.
struct WorldBox {
    glm::vec3 lowest{0.0F};
    glm::vec3 highest{0.0F};
};

/// For each of `selected`, the box around what `result` drew for it, its
/// corners taken back to the world by `inverseViewProjection`; nothing for
/// one that drew nothing.
[[nodiscard]] std::vector<std::optional<WorldBox>>
findOwnedBoxes(const n64::RspResult& result, const DrawnOwners& drawn,
               std::span<const usize> selected, const glm::mat4& inverseViewProjection);

/// The twelve edges of `box`, each its two ends.
[[nodiscard]] std::array<std::array<glm::vec3, 2>, 12> listBoxEdges(const WorldBox& box);

} // namespace toki::viewport
