#include "EditableBox.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

[[nodiscard]] usize findCharacterSize(const std::string_view text, const usize offset) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    usize size = 1;
    if (lead >= 0xF0) {
        size = 4;
    } else if (lead >= 0xE0) {
        size = 3;
    } else if (lead >= 0xC0) {
        size = 2;
    }
    return std::min(size, text.size() - offset);
}

[[nodiscard]] usize measure(const BoxUnit& unit) {
    if (const auto* text = std::get_if<TextPiece>(&unit.piece)) {
        return text->text.size();
    }
    return 1;
}

[[nodiscard]] usize measure(const std::span<const Piece> pieces) {
    usize total = 0;
    for (const Piece& piece : pieces) {
        const auto* text = std::get_if<TextPiece>(&piece);
        total += text != nullptr ? text->text.size() : 1;
    }
    return total;
}

void appendPiece(std::vector<Piece>& pieces, const Piece& piece) {
    const auto* text = std::get_if<TextPiece>(&piece);
    if (text != nullptr && !pieces.empty()) {
        if (auto* last = std::get_if<TextPiece>(&pieces.back())) {
            last->text += text->text;
            return;
        }
    }
    if (text == nullptr || !text->text.empty()) {
        pieces.push_back(piece);
    }
}

} // namespace

EditableBox::EditableBox(const std::span<const Piece> pieces, const Charmap* charmap)
    : charmap(charmap), units(makeUnits(pieces, charmap)), pieceCount(pieces.size()),
      caret(units.size()), anchor(caret) {}

void EditableBox::read(const std::span<const Piece> pieces) {
    units = makeUnits(pieces, charmap);
    pieceCount = pieces.size();
}

std::vector<BoxUnit> EditableBox::makeUnits(const std::span<const Piece> pieces,
                                            const Charmap* charmap) {
    std::vector<BoxUnit> units;
    for (usize index = 0; index < pieces.size(); ++index) {
        const auto* text = std::get_if<TextPiece>(&pieces[index]);
        if (text == nullptr) {
            units.push_back(BoxUnit{.piece = pieces[index], .source = {.piece = index}});
            continue;
        }
        usize offset = 0;
        while (offset < text->text.size()) {
            usize size = findCharacterSize(text->text, offset);
            if (charmap != nullptr) {
                if (const CharmapEntry* entry = charmap->findMatch(text->text, offset)) {
                    size = std::max(size, entry->text.size());
                }
            }
            units.push_back(BoxUnit{.piece = TextPiece{.text = text->text.substr(offset, size)},
                                    .source = {.piece = index, .offset = offset}});
            offset += size;
        }
    }
    return units;
}

std::vector<Piece> EditableBox::makePieces() const {
    std::vector<Piece> pieces;
    for (const BoxUnit& unit : units) {
        appendPiece(pieces, unit.piece);
    }
    return pieces;
}

UnitRange EditableBox::getSelection() const {
    return UnitRange{.begin = std::min(caret, anchor), .end = std::max(caret, anchor)};
}

void EditableBox::setCaret(const usize unit, const bool extends) {
    caret = std::min(unit, units.size());
    if (!extends) {
        anchor = caret;
    }
}

void EditableBox::moveLeft(const bool extends) {
    if (!extends && hasSelection()) {
        setCaret(getSelection().begin, false);
        return;
    }
    setCaret(caret == 0 ? 0 : caret - 1, extends);
}

void EditableBox::moveRight(const bool extends) {
    if (!extends && hasSelection()) {
        setCaret(getSelection().end, false);
        return;
    }
    setCaret(caret + 1, extends);
}

void EditableBox::moveToLineStart(const bool extends) {
    usize unit = caret;
    while (unit > 0 && !std::holds_alternative<NewlinePiece>(units[unit - 1].piece)) {
        --unit;
    }
    setCaret(unit, extends);
}

void EditableBox::moveToLineEnd(const bool extends) {
    usize unit = caret;
    while (unit < units.size() && !std::holds_alternative<NewlinePiece>(units[unit].piece)) {
        ++unit;
    }
    setCaret(unit, extends);
}

void EditableBox::selectAll() {
    anchor = 0;
    caret = units.size();
}

usize EditableBox::measureUntil(const usize unit) const {
    usize total = 0;
    for (usize index = 0; index < unit && index < units.size(); ++index) {
        total += measure(units[index]);
    }
    return total;
}

usize EditableBox::findUnitByMeasure(const usize target) const {
    usize total = 0;
    for (usize index = 0; index < units.size(); ++index) {
        if (total >= target) {
            return index;
        }
        total += measure(units[index]);
    }
    return units.size();
}

void EditableBox::replace(const UnitRange range, const std::span<const Piece> pieces) {
    std::vector<Piece> joined;
    for (usize index = 0; index < range.begin; ++index) {
        appendPiece(joined, units[index].piece);
    }
    for (const Piece& piece : pieces) {
        appendPiece(joined, piece);
    }
    for (usize index = range.end; index < units.size(); ++index) {
        appendPiece(joined, units[index].piece);
    }
    const usize caretMeasure = measureUntil(range.begin) + measure(pieces);
    read(joined);
    caret = findUnitByMeasure(caretMeasure);
    anchor = caret;
}

void EditableBox::insert(const std::span<const Piece> pieces) {
    replace(getSelection(), pieces);
}

void EditableBox::insertText(const std::string_view text) {
    std::vector<Piece> pieces;
    usize start = 0;
    for (usize index = 0; index <= text.size(); ++index) {
        if (index < text.size() && text[index] != '\n') {
            continue;
        }
        std::string line(text.substr(start, index - start));
        std::erase(line, '\r');
        if (!line.empty()) {
            pieces.emplace_back(TextPiece{.text = std::move(line)});
        }
        if (index < text.size()) {
            pieces.emplace_back(NewlinePiece{});
        }
        start = index + 1;
    }
    insert(pieces);
}

void EditableBox::eraseBackward() {
    if (hasSelection()) {
        replace(getSelection(), {});
    } else if (caret > 0) {
        replace(UnitRange{.begin = caret - 1, .end = caret}, {});
    }
}

void EditableBox::eraseForward() {
    if (hasSelection()) {
        replace(getSelection(), {});
    } else if (caret < units.size()) {
        replace(UnitRange{.begin = caret, .end = caret + 1}, {});
    }
}

std::vector<Piece> EditableBox::copySelection() const {
    std::vector<Piece> pieces;
    const UnitRange range = getSelection();
    for (usize index = range.begin; index < range.end; ++index) {
        appendPiece(pieces, units[index].piece);
    }
    return pieces;
}

std::optional<usize> EditableBox::findUnit(const TextPosition position) const {
    if (position.piece >= pieceCount) {
        return units.size();
    }
    std::optional<usize> found;
    for (usize index = 0; index < units.size(); ++index) {
        const TextPosition& source = units[index].source;
        if (source.piece == position.piece && source.offset <= position.offset) {
            found = index;
        } else if (source.piece > position.piece) {
            break;
        }
    }
    return found;
}

std::vector<CaretPlace> findCaretPlaces(const EditableBox& box, const LaidBox& laid,
                                        const std::span<const TextPosition> sources) {
    const usize count = box.getUnits().size() + 1;
    std::vector<std::optional<CaretPlace>> found(count);
    for (const LaidStop& stop : laid.stops) {
        if (stop.byte >= sources.size()) {
            continue;
        }
        const std::optional<usize> unit = box.findUnit(sources[stop.byte]);
        if (unit && !found[*unit]) {
            found[*unit] = CaretPlace{.x = stop.x, .y = stop.y};
        }
    }
    for (usize index = count - 1; index-- > 0;) {
        if (!found[index]) {
            found[index] = found[index + 1];
        }
    }
    std::optional<CaretPlace> last;
    std::vector<CaretPlace> places;
    places.reserve(count);
    for (const std::optional<CaretPlace>& place : found) {
        if (place) {
            last = place;
        }
        const s32 firstY = laid.lines.empty() ? laid.y : laid.lines.front().y;
        places.push_back(
            place.value_or(last.value_or(CaretPlace{.x = findLineStart(laid), .y = firstY})));
    }
    return places;
}

usize findPlaceAt(const std::span<const CaretPlace> places, const s32 lineHeight, const f32 x,
                  const f32 y) {
    std::optional<s32> lineY;
    f32 nearestLine = std::numeric_limits<f32>::max();
    for (const CaretPlace& place : places) {
        const f32 distance =
            std::abs(static_cast<f32>(place.y) + (static_cast<f32>(lineHeight) / 2.0F) - y);
        if (distance < nearestLine) {
            nearestLine = distance;
            lineY = place.y;
        }
    }
    usize nearest = places.empty() ? 0 : places.size() - 1;
    f32 nearestX = std::numeric_limits<f32>::max();
    for (usize index = 0; index < places.size(); ++index) {
        if (places[index].y != lineY) {
            continue;
        }
        const f32 distance = std::abs(static_cast<f32>(places[index].x) - x);
        if (distance < nearestX) {
            nearestX = distance;
            nearest = index;
        }
    }
    return nearest;
}

} // namespace toki::message
