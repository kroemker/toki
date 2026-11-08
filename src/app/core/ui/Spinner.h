#pragma once

#include "app/core/Types.h"

#include <imgui.h>

#include <string_view>

namespace toki::core {

/// A spinner `size` pixels square at the cursor, as an item, turning with
/// time. Shown wherever something is being read or written and what it
/// shows is not there yet or is about to change.
void drawSpinner(f32 size);

/// A spinner with a muted line under it, both centered in what is left of the
/// window, in place of what is not there yet.
void drawCenteredSpinner(std::string_view label);

/// Covers what the window drew from `start`, a position in the window as
/// `ImGui::GetCursorPos` gives it, `size` large or to the window's end where
/// a side is zero, with a translucent grey that takes the mouse, and a
/// centered spinner and `label` over it. Drawn after what it covers, child
/// windows included, since it is a child window drawn over them. The cursor
/// is left after it: moving it back over what was drawn would leave the
/// window's extent to a cursor no item validates, which Dear ImGui refuses.
void drawBusyCover(ImVec2 start, ImVec2 size, std::string_view label);

} // namespace toki::core
