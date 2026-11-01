#include "app/message/domain/MessageRows.h"

#include "app/message/domain/Message.h"
#include "app/message/domain/MessageReader.h"
#include "cparser/MacroTable.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using toki::message::Language;
using toki::message::MessageColumn;
using toki::message::MessageFilter;
using toki::message::MessageRow;
using toki::message::MessageTable;

namespace {

[[nodiscard]] MessageTable readFixture() {
    const cparser::MacroTable macros;
    return toki::message::readMessageTable(
        macros,
        toki::message::findMessageFiles(
            std::filesystem::path(TOKI_TEST_FIXTURES) / "app/message/fixtures/checkout", "test"));
}

[[nodiscard]] std::vector<base::u16> listIds(const std::vector<MessageRow>& rows) {
    std::vector<base::u16> ids;
    for (const MessageRow& row : rows) {
        ids.push_back(row.id);
    }
    return ids;
}

} // namespace

TEST_CASE("a message's row reads its text in a line, without its control codes", "[message]") {
    const MessageTable table = readFixture();
    const std::vector<MessageRow> rows =
        toki::message::makeMessageRows(table, Language::English, false, {});

    REQUIRE_FALSE(rows.empty());
    CHECK(rows[0].id == 0x0001);
    CHECK(rows[0].text ==
          "You borrowed a Pocket Egg! A Pocket Cucco will hatch from it overnight.");
    CHECK(rows[0].boxCount == 1);
    CHECK(rows[0].isVanilla);
    CHECK(rows[1].boxCount == 2);
}

TEST_CASE("the rows of a language leave out the rows of the others", "[message]") {
    const MessageTable table = readFixture();

    const std::vector<MessageRow> english =
        toki::message::makeMessageRows(table, Language::English, false, {});
    CHECK(listIds(english) == std::vector<base::u16>{0x0001, 0x0002, 0x0003, 0x8000, 0xFFFD});
    CHECK(english[2].boxType == "TEXTBOX_TYPE_BLACK");
}

TEST_CASE("rows are filtered by ID, text, source and box type", "[message]") {
    const MessageTable table = readFixture();

    CHECK(listIds(toki::message::makeMessageRows(table, Language::English, false,
                                                 MessageFilter{.text = "0x8000"})) ==
          std::vector<base::u16>{0x8000});
    CHECK(listIds(toki::message::makeMessageRows(table, Language::English, false,
                                                 MessageFilter{.text = "POCKET"})) ==
          std::vector<base::u16>{0x0001});
    CHECK(listIds(toki::message::makeMessageRows(table, Language::English, false,
                                                 MessageFilter{.showsVanilla = false})) ==
          std::vector<base::u16>{0x8000, 0xFFFD});
    CHECK(listIds(toki::message::makeMessageRows(
              table, Language::English, false,
              MessageFilter{.boxType = std::string("TEXTBOX_TYPE_BLUE")})) ==
          std::vector<base::u16>{0x0001});
}

TEST_CASE("rows sort by a column and keep table order among equals", "[message]") {
    const MessageTable table = readFixture();
    std::vector<MessageRow> rows =
        toki::message::makeMessageRows(table, Language::English, false, {});

    toki::message::sortMessageRows(rows, MessageColumn::Id, false);
    CHECK(rows.front().id == 0xFFFD);
    toki::message::sortMessageRows(rows, MessageColumn::Boxes, false);
    CHECK(rows.front().id == 0x0002);
}
