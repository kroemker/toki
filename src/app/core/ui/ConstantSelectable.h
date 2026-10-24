#pragma once

#include <string_view>

namespace toki::core {

/// One row of a dropdown of the checkout's constants: `label` as people read
/// it, "Heart Piece", and `name` as the code writes it, `GI_HEART_PIECE`, faint
/// at the row's right when the two differ, so either can be found. True when
/// it is clicked.
[[nodiscard]] bool drawConstantSelectable(std::string_view label, std::string_view name,
                                          bool isSelected);

/// Draws `name` faint at the right of the row the last item is on, when it
/// differs from the `label` that item shows.
void drawConstantName(std::string_view label, std::string_view name);

/// Whether a constant's row is shown for `filter`: whether its label or its
/// name holds it, ignoring case.
[[nodiscard]] bool isConstantFilteredIn(std::string_view label, std::string_view name,
                                        std::string_view filter);

} // namespace toki::core
