#pragma once

#include "app/scene/Types.h"
#include "base/Diagnostic.h"

#include <array>
#include <expected>
#include <span>

namespace toki::scene {

class SceneModel;

/// The most points a path holds: its count is a byte.
inline constexpr usize MaximumPathPoints = 0xFF;

/// Makes `points` the points of the path at `index` of the scene's header for
/// `layer`, the game's scene layer. The header, its path list and the path's
/// points are each copied for the layer first when another layer reads them
/// too, so an edit of one layer never shows in another. A path keeps at least
/// one point.
[[nodiscard]] std::expected<void, base::Diagnostic>
setPathPoints(SceneModel& model, usize layer, usize index,
              std::span<const std::array<s16, 3>> points);

/// Adds a path of `points` to the end of the layer's path list, made for the
/// header when it has none, and returns its index.
[[nodiscard]] std::expected<usize, base::Diagnostic>
addPath(SceneModel& model, usize layer, std::span<const std::array<s16, 3>> points);

/// Removes the path at `index`, and its points once nothing points at them.
/// The paths after it move down one, and actors name a path by its index.
/// Removing the last one removes the list's command from the header, and the
/// list once nothing points at it.
[[nodiscard]] std::expected<void, base::Diagnostic> removePath(SceneModel& model, usize layer,
                                                               usize index);

} // namespace toki::scene
