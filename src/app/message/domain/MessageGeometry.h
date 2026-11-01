#pragma once

#include "app/message/Types.h"
#include "base/Diagnostic.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>
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
    /// `sNonKanjiIndices` of `src/code/kanread.s`: where each Shift-JIS code
    /// below the kanji stands in the `kanji` font, 0xBC to a lead byte.
    std::vector<u16> nonKanjiIndices;
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads `sFontWidths` and the box position statics of `Message_Update`. A
/// table that is missing or not wholly literal is a diagnostic naming it, and
/// leaves its values zero.
[[nodiscard]] MessageGeometry readMessageGeometry(const hackeroot::Workspace& workspace);

/// Reads the `.half` values of `sNonKanjiIndices` out of `kanread.s`'s text.
[[nodiscard]] std::vector<u16> parseNonKanjiIndices(std::string_view assembly);

/// Where the glyph of the Shift-JIS `code` starts in the `kanji` font, as
/// `Kanji_OffsetFromShiftJIS` finds it; nothing for a code below the kanji
/// that the index table does not reach.
[[nodiscard]] std::optional<u32> findKanjiOffset(u16 code, const MessageGeometry& geometry);

} // namespace toki::message
