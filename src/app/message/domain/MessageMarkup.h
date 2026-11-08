#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::message {

/// One box of a message's text: what it shows, and the code that breaks to
/// the next box, `BOX_BREAK` or `BOX_BREAK_DELAYED(…)`, where one does.
struct BoxText {
    std::vector<Piece> pieces;
    std::optional<CodePiece> boxBreak;
};

/// The text split box by box at its box breaks.
[[nodiscard]] std::vector<BoxText> splitBoxes(const MessageText& text);

/// `sources`, places in all of `text`, as places in the pieces of the box each
/// falls in, as `splitBoxes` splits it. A box's break, and the `END` after the
/// last box, fall one past the box's last piece.
[[nodiscard]] std::vector<TextPosition> localizeSources(const MessageText& text,
                                                        std::span<const TextPosition> sources);

/// The pieces of `boxes` in order, each box's break after it.
[[nodiscard]] std::vector<Piece> joinBoxes(std::span<const BoxText> boxes);

/// `pieces` as a person edits them: text as it reads, a new line as a new
/// line, and each control code in braces, named in words. A color is its
/// name, `{Red}`; an item icon the item, `{Icon Pocket Egg}`; the player's name
/// `{Name}`; any other code its name and arguments, `{Shift 4}`,
/// `{Box Break Delayed 20}`. A brace of the text is doubled, `{{`. A box
/// break stands on a line of its own with an empty line after it, as the
/// checkout writes it; those line breaks are only layout, so a new line of the
/// text beside a box break is written `{New Line}`.
[[nodiscard]] std::string formatMarkup(std::span<const Piece> pieces);

/// What a run of markup is, for drawing it.
enum class MarkupSpanKind : u8 {
    Text,
    /// A doubled brace, which is text.
    Brace,
    NewLine,
    Code,
    BoxBreak,
    Color,
    /// A brace that opens a code never closed on its line.
    Open,
};

/// A run of markup from `begin` up to `end`; a color names the constant it
/// sets, `RED`.
struct MarkupSpan {
    usize begin = 0;
    usize end = 0;
    MarkupSpanKind kind = MarkupSpanKind::Text;
    std::string color;
};

/// `markup` cut into runs as `parseMarkup` reads it, every byte in one, a new
/// line a run of its own.
[[nodiscard]] std::vector<MarkupSpan> findMarkupSpans(std::string_view markup);

/// A mistake in markup and the byte of it the mistake starts at.
struct MarkupError {
    usize offset = 0;
    std::string message;
};

/// Reads `markup` back into pieces, as `formatMarkup` writes them. What reads
/// back is written as `formatMarkup` would write it again; line breaks right
/// before and after a box break are dropped, whatever their count; a brace that is
/// never closed, or one with nothing in it, is a mistake. A name that is no
/// code is taken for a color, which the encoder checks.
[[nodiscard]] std::expected<std::vector<Piece>, MarkupError> parseMarkup(std::string_view markup);

} // namespace toki::message
