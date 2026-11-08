#include "app/message/domain/MessageCodes.h"

#include "app/message/domain/Message.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using toki::message::CodePiece;

TEST_CASE("every code the rail puts in is one but colors and box breaks, with its arguments",
          "[message]") {
    const toki::message::InsertableCode* shift = toki::message::findInsertableCode("SHIFT");
    REQUIRE(shift != nullptr);
    const CodePiece made = toki::message::makeInitialCode(*shift);
    CHECK(made.name == "SHIFT");
    CHECK(made.arguments == std::vector<std::string>{"8"});

    CHECK(toki::message::findInsertableCode("TEXTID") != nullptr);
    CHECK(toki::message::findInsertableCode("COLOR") == nullptr);
    CHECK(toki::message::findInsertableCode("BOX_BREAK") == nullptr);
    CHECK(toki::message::listInsertableCodes().size() == 26);
}

TEST_CASE("a code is described in a few words by what the game does with it", "[message]") {
    CHECK(toki::message::describeCode(CodePiece{.name = "COLOR", .arguments = {"RED"}}) ==
          "Red Text");
    CHECK(toki::message::describeCode(CodePiece{.name = "UNSKIPPABLE"}) == "Unskippable Text");
    CHECK(toki::message::describeCode(CodePiece{.name = "SHIFT", .arguments = {"24"}}) ==
          "24 Pixels of Space");
    CHECK(toki::message::describeCode(CodePiece{.name = "TEXTID", .arguments = {"0x6023"}}) ==
          "Next Message 0x6023");
    CHECK(toki::message::describeCode(CodePiece{.name = "MARATHON_TIME"}) == "Marathon Time");
}
