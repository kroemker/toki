#pragma once

#include "app/viewport/Types.h"

#include <array>

namespace toki::viewport {

/// A point in the N64's clip space, as the RSP leaves its vertices.
using ClipPosition = std::array<f32, 4>;

/// A triangle's corners in clip space.
using ClipTriangle = std::array<ClipPosition, 3>;

} // namespace toki::viewport
