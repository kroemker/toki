#pragma once

#include "app/scene/Types.h"

#include <array>

namespace toki::scene {

/// A binary angle as the game stores one, 0x4000 a quarter turn, in degrees
/// from -180 up to 180. Every binary angle converts exactly.
[[nodiscard]] f32 convertToDegrees(s16 angle);

/// `degrees` as the nearest binary angle, turned into one turn.
[[nodiscard]] s16 convertToBinaryAngle(f32 degrees);

/// The rotation `rotation`, binary angles about X, Y and Z applied as the game
/// applies an actor's, Y then X then Z, turned `degrees` further about the
/// world's `axis`, right-handed. A turn about Y only adds to Y, exactly.
[[nodiscard]] std::array<s16, 3> rotateBinaryAngles(const std::array<s16, 3>& rotation, usize axis,
                                                    f32 degrees);

} // namespace toki::scene
