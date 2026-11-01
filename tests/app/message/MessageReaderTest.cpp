#include "app/message/domain/MessageReader.h"

#include "app/message/domain/Message.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/MacroTable.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using base::usize;
using toki::message::CodePiece;
using toki::message::Language;
using toki::message::Message;
using toki::message::MessageTable;
using toki::message::MessageText;
using toki::message::NewlinePiece;
using toki::message::Piece;
using toki::message::TextPiece;

namespace {

[[nodiscard]] std::filesystem::path getFixture(const std::string_view name) {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "app/message/fixtures" / name;
}

[[nodiscard]] MessageTable readFixture(const std::string_view name) {
    const cparser::MacroTable macros;
    return toki::message::readMessageTable(
        macros, toki::message::findMessageFiles(getFixture(name), "test"));
}

[[nodiscard]] const Message& findMessage(const MessageTable& table, const base::u16 id) {
    const auto found = std::ranges::find(table.messages, id, &Message::id);
    REQUIRE(found != table.messages.end());
    return *found;
}

[[nodiscard]] const MessageText& getEnglish(const Message& message) {
    return message.texts[static_cast<usize>(Language::English)];
}

[[nodiscard]] std::string getText(const Piece& piece) {
    REQUIRE(std::holds_alternative<TextPiece>(piece));
    return std::get<TextPiece>(piece).text;
}

[[nodiscard]] const CodePiece& getCode(const Piece& piece) {
    REQUIRE(std::holds_alternative<CodePiece>(piece));
    return std::get<CodePiece>(piece);
}

[[nodiscard]] bool reports(const MessageTable& table, const std::string_view text) {
    return std::ranges::any_of(table.diagnostics, [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("messages read from the included vanilla file first, then the checkout's own",
          "[message]") {
    const MessageTable table = readFixture("checkout");

    REQUIRE(table.diagnostics.empty());
    REQUIRE(table.messages.size() == 7);
    CHECK(table.messages[0].id == 0x0001);
    CHECK(table.messages[1].id == 0x0002);
    CHECK(table.messages[2].id == 0x0003);
    CHECK(table.messages[3].id == 0x0003);
    CHECK(table.messages[4].id == 0x8000);
    CHECK(table.messages[5].id == 0xFFFC);
    CHECK(table.messages[6].id == 0xFFFD);
    CHECK(table.messages[0].isVanilla);
    CHECK(table.messages[3].isVanilla);
    CHECK_FALSE(table.messages[4].isVanilla);
    CHECK_FALSE(table.messages[6].isVanilla);
}

TEST_CASE("a message keeps its box type, position and macro as written", "[message]") {
    const MessageTable table = readFixture("checkout");

    const Message& first = findMessage(table, 0x0001);
    CHECK(first.boxType == "TEXTBOX_TYPE_BLUE");
    CHECK(first.boxPosition == "TEXTBOX_POS_BOTTOM");
    CHECK(first.macro == toki::message::MessageMacro::Message);
    CHECK(findMessage(table, 0xFFFC).macro == toki::message::MessageMacro::FontMessage);
}

TEST_CASE("a message's text reads into text, line breaks and control codes", "[message]") {
    const MessageTable table = readFixture("checkout");
    const MessageText& english = getEnglish(findMessage(table, 0x0001));

    REQUIRE(english.isPresent);
    REQUIRE(english.pieces.size() == 13);
    CHECK(getCode(english.pieces[0]).name == "UNSKIPPABLE");
    CHECK(getCode(english.pieces[1]).name == "ITEM_ICON");
    CHECK(getCode(english.pieces[1]).arguments == std::vector<std::string>{"ITEM_POCKET_EGG"});
    CHECK(getCode(english.pieces[2]).name == "QUICKTEXT_ENABLE");
    CHECK(getText(english.pieces[3]) == "You borrowed a ");
    CHECK(getCode(english.pieces[4]).arguments == std::vector<std::string>{"RED"});
    CHECK(getText(english.pieces[5]) == "Pocket Egg");
    CHECK(getText(english.pieces[7]) == "!");
    CHECK(getCode(english.pieces[8]).name == "QUICKTEXT_DISABLE");
    CHECK(std::holds_alternative<NewlinePiece>(english.pieces[9]));
    CHECK(getText(english.pieces[10]) == "A Pocket Cucco will hatch from");
    CHECK(std::holds_alternative<NewlinePiece>(english.pieces[11]));
    CHECK(getText(english.pieces[12]) == "it overnight.");
}

TEST_CASE("adjacent strings join, escapes decode and charmap spellings stay as written",
          "[message]") {
    const MessageTable table = readFixture("checkout");
    const MessageText& english = getEnglish(findMessage(table, 0x0002));

    REQUIRE(english.pieces.size() >= 2);
    CHECK(getText(english.pieces[0]) == "Say \"hi\" to [A]?");
    CHECK(std::holds_alternative<NewlinePiece>(english.pieces[1]));
    const MessageText& custom = getEnglish(findMessage(table, 0x8000));
    REQUIRE(custom.pieces.size() == 3);
    CHECK(getCode(custom.pieces[1]).arguments == std::vector<std::string>{"12"});
}

TEST_CASE("a language without text keeps the word of its placeholder", "[message]") {
    const MessageTable table = readFixture("checkout");
    const Message& first = findMessage(table, 0x0001);

    const MessageText& german = first.texts[static_cast<usize>(Language::German)];
    CHECK_FALSE(german.isPresent);
    CHECK(german.placeholder == "MISSING");
    CHECK(first.texts[static_cast<usize>(Language::Japanese)].isPresent);
    CHECK(findMessage(table, 0xFFFC).texts[static_cast<usize>(Language::French)].placeholder ==
          "UNUSED");
    CHECK(getEnglish(findMessage(table, 0xFFFD)).isPresent);
}

TEST_CASE("one ID may be a Japanese row and a row for the other languages, each its own box",
          "[message]") {
    const MessageTable table = readFixture("checkout");
    const Message& japanese = table.messages[2];
    const Message& nes = table.messages[3];

    CHECK(japanese.macro == toki::message::MessageMacro::JapaneseOnly);
    CHECK(japanese.boxType == "TEXTBOX_TYPE_BLUE");
    CHECK(nes.macro == toki::message::MessageMacro::NesOnly);
    CHECK(nes.boxType == "TEXTBOX_TYPE_BLACK");
    CHECK(toki::message::isInLanguage(japanese, Language::Japanese, false));
    CHECK_FALSE(toki::message::isInLanguage(japanese, Language::English, false));
    CHECK(toki::message::isInLanguage(nes, Language::French, false));
    CHECK_FALSE(toki::message::isInLanguage(nes, Language::Japanese, false));
}

TEST_CASE("the font message is Japanese on NTSC and English on PAL", "[message]") {
    const MessageTable table = readFixture("checkout");
    const Message& font = findMessage(table, 0xFFFC);

    CHECK(toki::message::isInLanguage(font, Language::Japanese, false));
    CHECK_FALSE(toki::message::isInLanguage(font, Language::English, false));
    CHECK(toki::message::isInLanguage(font, Language::English, true));
    CHECK_FALSE(toki::message::isInLanguage(font, Language::Japanese, true));
}

TEST_CASE("a message under a conditional the build leaves out is not read", "[message]") {
    const MessageTable table = readFixture("checkout");

    CHECK(std::ranges::find(table.messages, base::u16{0x9000}, &Message::id) ==
          table.messages.end());
}

TEST_CASE("broken messages are diagnostics naming them, and the rest still read", "[message]") {
    const MessageTable table = readFixture("broken");

    CHECK(reports(table, "message 0x0010 is defined again"));
    CHECK(reports(table, "message 0x0011: `12` stands in a message's text"));
    CHECK(reports(table, "has 4 arguments where 7 were expected"));
    CHECK(reports(table, "message 0xFFFD is not last"));
    REQUIRE(table.messages.size() == 3);
    CHECK(table.messages[0].id == 0x0010);
    CHECK(table.messages[2].id == 0x0010);
}
