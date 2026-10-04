#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/ViewportFrame.h"

namespace toki::viewport {

/// The segment the reference grid is written to.
inline constexpr u8 ReferenceGridSegment = 0x06;

/// A checkerboard floor on the XZ plane, centred on the origin, as a memory
/// image of one display list built by hand. It is what the viewport shows
/// before anything is opened, and the scale a scene is read against: each
/// cell is 100 units, two of Link's heights.
[[nodiscard]] ViewportImage makeReferenceGrid();

} // namespace toki::viewport
