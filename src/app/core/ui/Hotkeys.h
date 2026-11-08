#pragma once

#include "app/core/domain/KeyBindings.h"

#include <string>
#include <string_view>

namespace toki::core {

/// The bindings every action's key is looked up in, from the settings.
void setKeyBindings(const KeyBindings& bindings);

[[nodiscard]] const KeyBindings& getKeyBindings();

/// Whether the key `action` is bound to was pressed this frame. A global
/// action is heard anywhere unless an item has taken the key; a viewport
/// action is heard only when asked, so the caller checks the cursor is over
/// the viewport, and never while text is typed. A message action is heard
/// only when asked too, while text is typed as well.
[[nodiscard]] bool isActionPressed(Action action);

/// Whether the key `action` is bound to was pressed this frame, whatever item
/// has taken it: for a field that takes a global action's key from the global
/// handler and does that action itself.
[[nodiscard]] bool isActionKeyPressed(Action action);

/// The key `action` is bound to as menus and tooltips show it, `Ctrl+,`;
/// empty when it is bound to none.
[[nodiscard]] std::string getShortcutLabel(Action action);

/// `text` with the key `action` is bound to after it in parentheses, as a
/// tooltip names it: `Move (W)`.
[[nodiscard]] std::string makeShortcutText(std::string_view text, Action action);

} // namespace toki::core
