#pragma once

#include "app/scene/Types.h"

namespace toki::scene {

/// A binary angle as the game stores one, 0x4000 a quarter turn, in degrees
/// from -180 up to 180. Every binary angle converts exactly.
[[nodiscard]] f32 convertToDegrees(s16 angle);

/// `degrees` as the nearest binary angle, turned into one turn.
[[nodiscard]] s16 convertToBinaryAngle(f32 degrees);

} // namespace toki::scene
