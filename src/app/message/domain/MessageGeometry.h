#pragma once

#include "app/message/Types.h"
#include "base/Diagnostic.h"

#include <array>
#include <filesystem>
#include <vector>

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::message {

inline constexpr usize FontCharacterCount = 144;
inline constexpr usize BoxTypeCount = 6;

/// Where a box of one type stands on the screen, by the position its message
/// asks for, and how far below its top the icon that ends it is drawn.
struct BoxPlacement {
    s16 x = 0;
    s16 lowerY = 0;
    s16 upperY = 0;
    s16 middleY = 0;
    s16 endIconOffset = 0;
};

/// The data tables `src/code/z_message.c` lays text out with, as the checkout
/// defines them: the advance of every character of the font and where each
/// box type is placed. What that file does in code rather than in a table is
/// mirrored by `layoutMessage`.
struct MessageGeometry {
    std::array<f32, FontCharacterCount> fontWidths{};
    std::array<BoxPlacement, BoxTypeCount> boxes{};
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads `sFontWidths` and the box position statics of `Message_Update`. A
/// table that is missing or not wholly literal is a diagnostic naming it, and
/// leaves its values zero.
[[nodiscard]] MessageGeometry readMessageGeometry(const hackeroot::Workspace& workspace);

} // namespace toki::message
