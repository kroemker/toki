#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Charmap.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageLayout.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace toki::message {

/// One step of a box's text a caret moves over: a character as the charmap
/// reads it, so `[A]` is one, a new line, or a control code.
struct BoxUnit {
    Piece piece;
    /// Where the unit starts in the pieces the box was made from.
    TextPosition source;
};

/// The units from `begin` up to `end`.
struct UnitRange {
    usize begin = 0;
    usize end = 0;
};

/// One box's text as it is typed in: its units, the caret before one of them
/// or after the last, and the anchor a selection reaches from it to. Every
/// edit makes the text's pieces again and reads them back into units, so a
/// sign the charmap knows, typed key by key, becomes one unit once complete.
class EditableBox {
public:
    /// `charmap` says which runs of text are one character; without one,
    /// every UTF-8 character is.
    EditableBox(std::span<const Piece> pieces, const Charmap* charmap);

    /// The text as pieces, runs of text joined.
    [[nodiscard]] std::vector<Piece> makePieces() const;

    [[nodiscard]] std::span<const BoxUnit> getUnits() const {
        return units;
    }

    [[nodiscard]] usize getCaret() const {
        return caret;
    }

    [[nodiscard]] usize getAnchor() const {
        return anchor;
    }

    [[nodiscard]] bool hasSelection() const {
        return caret != anchor;
    }

    [[nodiscard]] UnitRange getSelection() const;

    /// Puts the caret before unit `unit`, or after the last; `extends` keeps
    /// the anchor where it is, so what lies between is selected.
    void setCaret(usize unit, bool extends);

    /// Moves the caret one unit; without `extends`, a selection is left at
    /// its edge instead.
    void moveLeft(bool extends);
    void moveRight(bool extends);

    /// Moves the caret to where its line starts or ends, by the box's new
    /// lines.
    void moveToLineStart(bool extends);
    void moveToLineEnd(bool extends);

    void selectAll();

    /// Puts `pieces` where the caret is, over what is selected, and the caret
    /// after them.
    void insert(std::span<const Piece> pieces);

    /// Draws what is selected in `color`, a `COLOR` code: puts `color` before
    /// it and, after it, the color the text after it was drawn in, the last
    /// `COLOR` before the selection's end or else `initial`. Color codes in and
    /// right beside the selection go, so a selection colored twice is wrapped
    /// once; a code is left out where the color it sets is already drawn in.
    /// What was selected stays selected.
    void colorSelection(const CodePiece& color, const CodePiece& initial);

    /// Puts UTF-8 `text` where the caret is, a line break of it as a new line.
    void insertText(std::string_view text);

    /// Removes what is selected, or else the unit before the caret.
    void eraseBackward();

    /// Removes what is selected, or else the unit after the caret.
    void eraseForward();

    /// What is selected, as pieces.
    [[nodiscard]] std::vector<Piece> copySelection() const;

    /// The unit `position` of the box's pieces falls in; one past the last
    /// piece is after the last unit.
    [[nodiscard]] std::optional<usize> findUnit(TextPosition position) const;

private:
    [[nodiscard]] static std::vector<BoxUnit> makeUnits(std::span<const Piece> pieces,
                                                        const Charmap* charmap);
    void read(std::span<const Piece> pieces);
    void replace(UnitRange range, std::span<const Piece> pieces);
    [[nodiscard]] usize measureUntil(usize unit) const;
    [[nodiscard]] usize findUnitByMeasure(usize target) const;

    const Charmap* charmap;
    std::vector<BoxUnit> units;
    usize pieceCount = 0;
    usize caret = 0;
    usize anchor = 0;
};

/// Where a caret before a unit is drawn, at the top of its line.
struct CaretPlace {
    s32 x = 0;
    s32 y = 0;
};

/// Where the caret stands in `laid` before each unit of `box`, and after the
/// last: where the game's pen stood when it reached the unit. A unit the game
/// drew nothing for stands where the next does. `sources` says where each
/// byte of the message was encoded from, in the box's own pieces
/// (`localizeSources`).
[[nodiscard]] std::vector<CaretPlace> findCaretPlaces(const EditableBox& box, const LaidBox& laid,
                                                      std::span<const TextPosition> sources);

/// The caret place nearest to `x` on the line nearest to `y`, both in screen
/// pixels, where a click there puts the caret.
[[nodiscard]] usize findPlaceAt(std::span<const CaretPlace> places, s32 lineHeight, f32 x, f32 y);

} // namespace toki::message
