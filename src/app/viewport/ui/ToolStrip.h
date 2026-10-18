#pragma once

#include "app/viewport/domain/ViewportTools.h"

#include <imgui.h>

#include <optional>

namespace toki::viewport {

/// Draws the selection modes as a group of icon buttons in the viewport's bar,
/// those not built yet disabled. True when one was picked.
bool drawSelectionModes(ViewportTools& tools);

/// Draws the tools as a vertical group of icon buttons floating over the
/// image at `position`, those not built yet disabled.
void drawToolStrip(ViewportTools& tools, ImVec2 position);

/// Draws how a pick changes the selection as a group of icon buttons in the
/// viewport's bar, `shown` lit: the one a held key picks, or the one clicked.
void drawPickModes(ViewportTools& tools, PickMode shown);

/// How a pick changes the selection while Shift or Ctrl is held: Shift adds,
/// Ctrl takes away. Nothing while neither is.
[[nodiscard]] std::optional<PickMode> findHeldPickMode();

/// The tools as the keys bound to them, pressed this frame, change them.
/// Called only while the cursor is over the viewport.
void applyToolHotkeys(ViewportTools& tools);

} // namespace toki::viewport
