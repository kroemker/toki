#include "app/message/domain/MessageMarkup.h"

#include "app/message/domain/Message.h"
#include "app/message/domain/MessageReader.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <variant>
#include <vector>

using base::usize;
using toki::message::BoxText;
using toki::message::CodePiece;
using toki::message::NewlinePiece;
using toki::message::Piece;
using toki::message::TextPiece;

namespace {

[[nodiscard]] std::string describe(const std::vector<Piece>& pieces) {
    std::string described;
    for (const Piece& piece : pieces) {
        if (const auto* text = std::get_if<TextPiece>(&piece)) {
            described += std::format("\"{}\" ", text->text);
        } else if (std::holds_alternative<NewlinePiece>(piece)) {
            described += "\\n ";
        } else {
            const CodePiece& code = std::get<CodePiece>(piece);
            described += code.name + "(";
            for (const std::string& argument : code.arguments) {
                described += argument + ",";
            }
            described += ") ";
        }
    }
    return described;
}

[[nodiscard]] std::vector<Piece> parse(const std::string_view markup) {
    std::expected<std::vector<Piece>, toki::message::MarkupError> pieces =
        toki::message::parseMarkup(markup);
    REQUIRE(pieces.has_value());
    return *pieces;
}

} // namespace

TEST_CASE("markup names colors, icons and codes in words", "[message]") {
    const std::vector<Piece> pieces = {
        TextPiece{.text = "You got the "},
        CodePiece{.name = "COLOR", .arguments = {"RED"}},
        TextPiece{.text = "Pocket Egg"},
        CodePiece{.name = "COLOR", .arguments = {"DEFAULT"}},
        TextPiece{.text = ", "},
        CodePiece{.name = "NAME"},
        TextPiece{.text = "!"},
        NewlinePiece{},
        CodePiece{.name = "ITEM_ICON", .arguments = {"ITEM_POCKET_EGG"}},
        CodePiece{.name = "SHIFT", .arguments = {"4"}},
        CodePiece{.name = "HIGHSCORE", .arguments = {"HS_HORSE_RACE"}},
        CodePiece{.name = "SFX", .arguments = {"NA_SE_EN_NUTS_FAINT"}},
        CodePiece{.name = "BACKGROUND", .arguments = {"0", "1", "2"}},
        TextPiece{.text = "{braces}"},
    };

    const std::string markup = toki::message::formatMarkup(pieces);

    CHECK(markup == "You got the {Red}Pocket Egg{Default}, {Name}!\n{Icon Pocket Egg}{Shift 4}"
                    "{Highscore Horse Race}{Sfx NA_SE_EN_NUTS_FAINT}{Background 0, 1, 2}"
                    "{{braces}}");
    CHECK(describe(parse(markup)) == describe(pieces));
}

TEST_CASE("markup reads a name that is no code as a color, and keeps constants as written",
          "[message]") {
    CHECK(describe(parse("{Lightblue}{Icon ITEM_BOMB}{Color 0x41}")) ==
          "COLOR(LIGHTBLUE,) ITEM_ICON(ITEM_BOMB,) COLOR(0x41,) ");
}

TEST_CASE("markup with a brace left open or closing nothing is a mistake at that brace",
          "[message]") {
    const auto open = toki::message::parseMarkup("ab{Red");
    REQUIRE_FALSE(open.has_value());
    CHECK(open.error().offset == 2);

    const auto stray = toki::message::parseMarkup("a}b");
    REQUIRE_FALSE(stray.has_value());
    CHECK(stray.error().offset == 1);

    CHECK_FALSE(toki::message::parseMarkup("{}").has_value());
}

TEST_CASE("a box break stands on a line of its own, and a new line beside it is written out",
          "[message]") {
    const std::vector<Piece> pieces = {TextPiece{.text = "One"},
                                       CodePiece{.name = "BOX_BREAK"},
                                       TextPiece{.text = "Two"},
                                       NewlinePiece{},
                                       CodePiece{.name = "BOX_BREAK_DELAYED", .arguments = {"20"}},
                                       NewlinePiece{},
                                       TextPiece{.text = "Three"}};

    const std::string markup = toki::message::formatMarkup(pieces);

    CHECK(markup == "One\n{Box Break}\n\nTwo{New Line}\n{Box Break Delayed 20}\n\n{New Line}Three");
    CHECK(describe(parse(markup)) == describe(pieces));
    CHECK(describe(parse("One{Box Break}Two")) == describe(parse("One\n\n{Box Break}\n\n\nTwo")));
}

TEST_CASE("markup is cut into runs of text, codes, colors and box breaks", "[message]") {
    using toki::message::MarkupSpanKind;
    const std::vector<toki::message::MarkupSpan> spans =
        toki::message::findMarkupSpans("a {Red}b{{\n{Box Break}{Shift 4}{Op");

    REQUIRE(spans.size() == 8);
    CHECK(spans[0].kind == MarkupSpanKind::Text);
    CHECK(spans[0].end == 2);
    CHECK(spans[1].kind == MarkupSpanKind::Color);
    CHECK(spans[1].color == "RED");
    CHECK(spans[2].kind == MarkupSpanKind::Text);
    CHECK(spans[3].kind == MarkupSpanKind::Brace);
    CHECK(spans[4].kind == MarkupSpanKind::NewLine);
    CHECK(spans[5].kind == MarkupSpanKind::BoxBreak);
    CHECK(spans[6].kind == MarkupSpanKind::Code);
    CHECK(spans[7].kind == MarkupSpanKind::Open);
    CHECK(spans[7].end == 34);
}

TEST_CASE("a message splits into boxes at its box breaks and joins back as it was", "[message]") {
    toki::message::MessageText text;
    text.pieces = {
        TextPiece{.text = "One"}, CodePiece{.name = "BOX_BREAK"}, TextPiece{.text = "Two"},
        CodePiece{.name = "BOX_BREAK_DELAYED", .arguments = {"20"}}, TextPiece{.text = "Three"}};

    const std::vector<BoxText> boxes = toki::message::splitBoxes(text);

    REQUIRE(boxes.size() == 3);
    CHECK(toki::message::formatMarkup(boxes[1].pieces) == "Two");
    REQUIRE(boxes[1].boxBreak.has_value());
    CHECK(boxes[1].boxBreak->name == "BOX_BREAK_DELAYED");
    CHECK_FALSE(boxes[2].boxBreak.has_value());
    CHECK(describe(toki::message::joinBoxes(boxes)) == describe(text.pieces));
}

TEST_CASE("every message of a real checkout reads back from its markup as it was",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const toki::message::MessageTable table = toki::message::readMessages(*workspace);

    usize texts = 0;
    for (const toki::message::Message& message : table.messages) {
        for (const toki::message::MessageText& text : message.texts) {
            const std::string markup = toki::message::formatMarkup(text.pieces);
            const auto parsed = toki::message::parseMarkup(markup);
            INFO(std::format("0x{:04X}: {}", message.id, markup));
            REQUIRE(parsed.has_value());
            REQUIRE(describe(*parsed) == describe(text.pieces));
            ++texts;
        }
    }
    CHECK(texts > 0);
}
