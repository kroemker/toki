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
/// `{Box Break Delayed 20}`. A brace of the text is doubled, `{{`.
[[nodiscard]] std::string formatMarkup(std::span<const Piece> pieces);

/// A mistake in markup and the byte of it the mistake starts at.
struct MarkupError {
    usize offset = 0;
    std::string message;
};

/// Reads `markup` back into pieces, as `formatMarkup` writes them. What reads
/// back is written as `formatMarkup` would write it again; a brace that is
/// never closed, or one with nothing in it, is a mistake. A name that is no
/// code is taken for a color, which the encoder checks.
[[nodiscard]] std::expected<std::vector<Piece>, MarkupError> parseMarkup(std::string_view markup);

} // namespace toki::message
