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

TEST_CASE("a code is named as the rail's button names it, a color by the text it draws",
          "[message]") {
    CHECK(toki::message::makeCodeName(CodePiece{.name = "COLOR", .arguments = {"RED"}}) ==
          "Red Text");
    CHECK(toki::message::makeCodeName(CodePiece{.name = "UNSKIPPABLE"}) == "Unskippable");
    CHECK(toki::message::makeCodeName(CodePiece{.name = "SHIFT", .arguments = {"24"}}) == "Shift");
    CHECK(toki::message::makeCodeName(
              CodePiece{.name = "BOX_BREAK_DELAYED", .arguments = {"20"}}) == "Box Break Delayed");
    CHECK(toki::message::makeCodeName(
              CodePiece{.name = "ITEM_ICON", .arguments = {"ITEM_POCKET_EGG"}}) == "Icon");
}
