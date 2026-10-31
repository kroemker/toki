#pragma once

#include "app/core/domain/KeyBindings.h"
#include "app/viewport/domain/ViewportTools.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace toki::viewport {

/// Draws the selection modes as a group of icon buttons in the viewport's bar,
/// those of faces, edges and vertices disabled while `canEdit` is false, as
/// when what is shown offers nothing to edit by them. True when one was
/// picked.
bool drawSelectionModes(ViewportTools& tools, bool canEdit);

/// A button the content's owner adds below the tools while the viewport
/// selects by faces, edges or vertices: what is done to what is picked.
struct StripAction {
    const char* icon = "";
    std::string name;
    std::optional<core::Action> action;
    /// Why it cannot be done now; empty when it can.
    std::string whyNot;
    std::function<void()> run;
};

/// Draws the tools as a vertical group of icon buttons floating over the
/// image at `position`, those not built yet disabled, and `actions` below
/// them.
void drawToolStrip(ViewportTools& tools, ImVec2 position, std::span<const StripAction> actions);

/// Draws how a pick changes the selection as a group of icon buttons in the
/// viewport's bar, `shown` lit: the one a held key picks, or the one clicked.
void drawPickModes(ViewportTools& tools, PickMode shown);

/// How a pick changes the selection while Shift or Ctrl is held: Shift adds,
/// Ctrl takes away. Nothing while neither is.
[[nodiscard]] std::optional<PickMode> findHeldPickMode();

/// The tools as the keys bound to them, pressed this frame, change them: a
/// mode, which is left at node mode while `canEdit` is false, and the key
/// that toggles between node mode and the mode of components last picked.
/// Called only while the cursor is over the viewport.
void applyToolHotkeys(ViewportTools& tools, bool canEdit);

/// Picks `mode`, remembering it when it is one of faces, edges or vertices.
void setSelectionMode(ViewportTools& tools, SelectionMode mode);

} // namespace toki::viewport
