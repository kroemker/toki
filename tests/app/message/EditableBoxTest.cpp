#include "app/message/domain/EditableBox.h"

#include "app/message/domain/Charmap.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageMarkup.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using base::usize;
using toki::message::CaretPlace;
using toki::message::Charmap;
using toki::message::CodePiece;
using toki::message::EditableBox;
using toki::message::LaidBox;
using toki::message::LaidStop;
using toki::message::NewlinePiece;
using toki::message::Piece;
using toki::message::TextPiece;
using toki::message::TextPosition;

namespace {

[[nodiscard]] Charmap makeCharmap() {
    return Charmap{.entries = {{.text = "[A]", .code = 0x9F}, {.text = "\xC3\xA9", .code = 0x96}}};
}

[[nodiscard]] std::vector<Piece> parse(const std::string_view markup) {
    std::expected<std::vector<Piece>, toki::message::MarkupError> pieces =
        toki::message::parseMarkup(markup);
    REQUIRE(pieces.has_value());
    return *pieces;
}

[[nodiscard]] std::string format(const EditableBox& box) {
    return toki::message::formatMarkup(box.makePieces());
}

} // namespace

TEST_CASE("a box's units are its characters as the charmap reads them, new lines and codes",
          "[message]") {
    const Charmap charmap = makeCharmap();
    const EditableBox box(parse("Press [A]\n{Red}d\xC3\xA9j\xC3\xA0"), &charmap);

    const std::span units = box.getUnits();
    REQUIRE(units.size() == 13);
    CHECK(std::get<TextPiece>(units[6].piece).text == "[A]");
    CHECK(units[6].source == TextPosition{.piece = 0, .offset = 6});
    CHECK(std::holds_alternative<NewlinePiece>(units[7].piece));
    CHECK(std::get<CodePiece>(units[8].piece).name == "COLOR");
    CHECK(std::get<TextPiece>(units[10].piece).text == "\xC3\xA9");
    CHECK(std::get<TextPiece>(units[12].piece).text == "\xC3\xA0");
    CHECK(box.getCaret() == 13);
    CHECK(format(box) == "Press [A]\n{Red}d\xC3\xA9j\xC3\xA0");
}

TEST_CASE("typed text goes in at the caret, and a sign typed key by key becomes one unit",
          "[message]") {
    const Charmap charmap = makeCharmap();
    EditableBox box(parse("Hi!"), &charmap);
    box.setCaret(2, false);

    box.insertText(" [A");
    CHECK(box.getCaret() == 5);
    box.insertText("]");
    CHECK(format(box) == "Hi [A]!");
    CHECK(box.getUnits().size() == 5);
    CHECK(box.getCaret() == 4);

    box.insertText("a\nb");
    CHECK(format(box) == "Hi [A]a\nb!");
    CHECK(box.getCaret() == 7);
}

TEST_CASE("erasing takes the unit beside the caret, a code whole, or what is selected",
          "[message]") {
    EditableBox box(parse("A{Red}B{Default}C"), nullptr);
    box.setCaret(2, false);

    box.eraseBackward();
    CHECK(format(box) == "AB{Default}C");
    CHECK(box.getCaret() == 1);

    box.eraseForward();
    CHECK(format(box) == "A{Default}C");

    box.setCaret(0, false);
    box.setCaret(2, true);
    CHECK(box.hasSelection());
    box.eraseBackward();
    CHECK(format(box) == "C");
    CHECK(box.getCaret() == 0);
    CHECK_FALSE(box.hasSelection());
}

TEST_CASE("a selection is copied as pieces and replaced by what is put in", "[message]") {
    EditableBox box(parse("One {Red}two{Default} three"), nullptr);
    box.setCaret(4, false);
    box.setCaret(9, true);

    CHECK(toki::message::formatMarkup(box.copySelection()) == "{Red}two{Default}");
    box.insert(parse("{Name}"));
    CHECK(format(box) == "One {Name} three");
    CHECK(box.getCaret() == 5);
}

TEST_CASE("the caret moves by units, to a line's ends, and collapses a selection", "[message]") {
    EditableBox box(parse("ab\ncd"), nullptr);
    box.setCaret(4, false);

    box.moveToLineStart(false);
    CHECK(box.getCaret() == 3);
    box.moveLeft(false);
    CHECK(box.getCaret() == 2);
    box.moveToLineStart(true);
    CHECK(box.getCaret() == 0);
    box.moveRight(false);
    CHECK(box.getCaret() == 2);
    box.moveToLineEnd(false);
    CHECK(box.getCaret() == 2);
    box.moveRight(false);
    box.moveToLineEnd(false);
    CHECK(box.getCaret() == 5);
    box.selectAll();
    CHECK(box.getSelection().begin == 0);
    CHECK(box.getSelection().end == 5);
}

TEST_CASE("a position in the box's pieces falls in the unit it is part of", "[message]") {
    const Charmap charmap = makeCharmap();
    const EditableBox box(parse("x[A]{Red}"), &charmap);

    CHECK(box.findUnit({.piece = 0, .offset = 0}) == 0);
    CHECK(box.findUnit({.piece = 0, .offset = 1}) == 1);
    CHECK(box.findUnit({.piece = 0, .offset = 2}) == 1);
    CHECK(box.findUnit({.piece = 1}) == 2);
    CHECK(box.findUnit({.piece = 2}) == 3);
}

TEST_CASE("sources of a message are made places in the pieces of each box", "[message]") {
    const toki::message::MessageText text{.pieces = parse("ab{Box Break}c{Red}"),
                                          .isPresent = true};
    const std::vector<TextPosition> sources = {
        {.piece = 0, .offset = 1}, {.piece = 1}, {.piece = 2}, {.piece = 3}, {.piece = 4}};

    const std::vector<TextPosition> local = toki::message::localizeSources(text, sources);

    REQUIRE(local.size() == 5);
    CHECK(local[0] == TextPosition{.piece = 0, .offset = 1});
    CHECK(local[1] == TextPosition{.piece = 1});
    CHECK(local[2] == TextPosition{.piece = 0});
    CHECK(local[3] == TextPosition{.piece = 1});
    CHECK(local[4] == TextPosition{.piece = 2});
}

TEST_CASE("the caret stands where the pen stood, and one the game drew nothing for where the "
          "next does",
          "[message]") {
    const EditableBox box(parse("ab{Name}"), nullptr);
    LaidBox laid;
    laid.lines.push_back({.y = 160, .startX = 65, .endX = 100});
    laid.stops = {LaidStop{.byte = 0, .x = 65, .y = 160}, LaidStop{.byte = 2, .x = 77, .y = 160},
                  LaidStop{.byte = 3, .x = 101, .y = 160}};
    const std::vector<TextPosition> sources = {
        {.piece = 0, .offset = 0}, {.piece = 0, .offset = 1}, {.piece = 1}, {.piece = 2}};

    const std::vector<CaretPlace> places = toki::message::findCaretPlaces(box, laid, sources);

    REQUIRE(places.size() == 4);
    CHECK(places[0].x == 65);
    CHECK(places[1].x == 77);
    CHECK(places[2].x == 77);
    CHECK(places[3].x == 101);

    CHECK(toki::message::findPlaceAt(places, 12, 80.0F, 165.0F) == 1);
    CHECK(toki::message::findPlaceAt(places, 12, 300.0F, 100.0F) == 3);
}

TEST_CASE("a charmap's signs are its buttons and what lies past the Latin letters", "[message]") {
    const Charmap charmap{.entries = {{.text = "\n", .code = 0x01},
                                      {.text = "[A]", .code = 0x9F},
                                      {.text = "\xE2\x96\xBC", .code = 0xA9},
                                      {.text = "\xC3\xA9", .code = 0x96}}};

    const std::vector<const toki::message::CharmapEntry*> signs = charmap.listSigns();

    REQUIRE(signs.size() == 2);
    CHECK(signs[0]->text == "[A]");
    CHECK(signs[1]->code == 0xA9);
}
