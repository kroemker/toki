#pragma once

#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportView.h"

#include <optional>

namespace toki::viewport {

/// What the bar was used to change this frame; nothing where it was not.
struct ViewportBarEdit {
    std::optional<ViewportView> view;
    std::optional<ViewportChoices> choices;
};

/// Draws the bar above the viewport's image: the Shading popover, and the
/// Overlays popover with its switch for all of them. A row the content does
/// not offer a choice for is left out.
[[nodiscard]] ViewportBarEdit drawViewportBar(const ViewportView& view,
                                              const std::optional<ViewportChoices>& choices);

/// Draws the edit targets `choices` offers as a group of buttons, the one
/// picked lit, and the link toggle where it offers one. The choices as they
/// were changed, or nothing.
[[nodiscard]] std::optional<ViewportChoices> drawEditTargets(const ViewportChoices& choices);

/// The view as the bar's hotkeys pressed this frame change it, or nothing.
/// Called only while the cursor is over the viewport.
[[nodiscard]] std::optional<ViewportView> applyViewportHotkeys(const ViewportView& view);

} // namespace toki::viewport
