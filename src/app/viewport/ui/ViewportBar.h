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

/// Draws the bar above the viewport's image: the Shading popover, a switch for
/// each common overlay where the bar is wide enough, and the Overlays popover
/// with its switch for all of them. A row the content does not offer a choice
/// for is left out.
[[nodiscard]] ViewportBarEdit drawViewportBar(const ViewportView& view,
                                              const std::optional<ViewportChoices>& choices);

/// A button labelled `label` that opens `popup` under it. True while the
/// popup is open, which must then be ended.
[[nodiscard]] bool beginBarMenu(const std::string& label, const char* popup);

/// The view as the bar's hotkeys pressed this frame change it, or nothing.
/// Called only while the cursor is over the viewport.
[[nodiscard]] std::optional<ViewportView> applyViewportHotkeys(const ViewportView& view);

} // namespace toki::viewport
