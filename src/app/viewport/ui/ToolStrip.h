#pragma once

#include "app/viewport/domain/ViewportTools.h"

#include <imgui.h>

namespace toki::viewport {

/// Draws the selection modes as a group of icon buttons in the viewport's bar,
/// those not built yet disabled. True when one was picked.
bool drawSelectionModes(ViewportTools& tools);

/// Draws the tools as a vertical group of icon buttons floating over the
/// image at `position`, those not built yet disabled.
void drawToolStrip(ViewportTools& tools, ImVec2 position);

/// The tools as the keys bound to them, pressed this frame, change them.
/// Called only while the cursor is over the viewport.
void applyToolHotkeys(ViewportTools& tools);

} // namespace toki::viewport
