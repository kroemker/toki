#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"

#include <glm/ext/vector_float3.hpp>

#include <array>

namespace toki::scene {

/// A side of a waterbox, which its handle in the viewport moves.
enum class WaterBoxSide : u8 {
    MinX,
    MaxX,
    MinZ,
    MaxZ,
};

inline constexpr std::array<WaterBoxSide, 4> WaterBoxSides{WaterBoxSide::MinX, WaterBoxSide::MaxX,
                                                           WaterBoxSide::MinZ, WaterBoxSide::MaxZ};

/// The middle of the waterbox's surface, where it is moved from.
[[nodiscard]] glm::vec3 findWaterBoxCenter(const WaterBox& box);

/// The waterbox moved so its surface's middle is at `center`, as near as
/// whole units come, its size kept.
[[nodiscard]] WaterBox makeMovedWaterBox(const WaterBox& box, const glm::vec3& center);

/// The middle of the edge of the waterbox's surface along `side`.
[[nodiscard]] glm::vec3 findWaterBoxSidePoint(const WaterBox& box, WaterBoxSide side);

/// Which way `side` faces, out of the waterbox.
[[nodiscard]] glm::vec3 getWaterBoxSideDirection(WaterBoxSide side);

/// The waterbox with `side` moved `distance` units outwards, or inwards when
/// it is negative, the other side kept. It is never less than one unit across,
/// nor reaches past what an `s16` holds.
[[nodiscard]] WaterBox makeResizedWaterBox(const WaterBox& box, WaterBoxSide side, s32 distance);

} // namespace toki::scene
