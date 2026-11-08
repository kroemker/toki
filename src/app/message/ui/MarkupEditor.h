#pragma once

#include "app/message/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct ImFont;

namespace toki::message {

/// The color a `COLOR` constant draws text in, if it is one the checkout has.
using FindMarkupColor = std::function<std::optional<u32>(std::string_view constant)>;

/// A multiline field of markup filling what is left of the window, its codes
/// drawn in colors of their own and a color code in the color it sets. Dear
/// ImGui's field draws the caret and the selection; the text is drawn over it,
/// run by run, at the places the field lays it out at in `font`. The field
/// keeps no undo steps of its own, since the draft does. `isReloaded` when
/// `markup` was changed from outside, so a field being typed in takes it, its
/// selection kept. True when the text changed.
[[nodiscard]] bool drawMarkupEditor(const char* id, std::string& markup, ImFont* font, f32 fontSize,
                                    const FindMarkupColor& findColor, bool isReloaded);

} // namespace toki::message
