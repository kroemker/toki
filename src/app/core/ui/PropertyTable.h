#pragma once

#include <string_view>

namespace toki::core {

/// Starts a table of property rows, the layout every form of the application
/// uses: each row a label on the left and its input filling the rest. A row is
/// begun with `drawPropertyLabel`, and the table ended with `endProperties`
/// when this returns true. `labelWidth` is the label column's width, in
/// multiples of the font size.
[[nodiscard]] bool beginProperties(const char* id, float labelWidth = 9.0F);

void endProperties();

/// Starts one property row with `label` on the left, `help` shown when it is
/// hovered, and readies the input column for the widget drawn next, which
/// fills it.
void drawPropertyLabel(std::string_view label, std::string_view help = {});

} // namespace toki::core
