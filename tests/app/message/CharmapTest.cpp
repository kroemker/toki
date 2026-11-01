#include "app/message/domain/Charmap.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>

using toki::message::Charmap;

TEST_CASE("a charmap reads its sequences in the order the file lists them", "[message]") {
    const std::expected<Charmap, std::string> charmap = toki::message::parseCharmap(R"(
# Determines how certain text sequences are encoded.
{
    '\n'    : 0x01,

    '[A]'   : 0x9F,
    "[C-Up]": 0xA5,
    'é'     : 0x96,
    'à': 144,
}
)");

    REQUIRE(charmap.has_value());
    REQUIRE(charmap->entries.size() == 5);
    CHECK(charmap->entries[0].text == "\n");
    CHECK(charmap->entries[0].code == 0x01);
    CHECK(charmap->entries[2].text == "[C-Up]");
    CHECK(charmap->entries[3].text == "\xC3\xA9");
    CHECK(charmap->entries[4].text == "\xC3\xA0");
    CHECK(charmap->entries[4].code == 144);
    CHECK(charmap->findCode("[A]") == 0x9F);
    CHECK_FALSE(charmap->findCode("[B]").has_value());
}

TEST_CASE("a charmap match is the first entry the text starts with", "[message]") {
    const std::expected<Charmap, std::string> charmap =
        toki::message::parseCharmap("{ 'a': 1, 'ab': 2, '[C-Up]': 3 }");

    REQUIRE(charmap.has_value());
    const auto* first = charmap->findMatch("xab", 1);
    REQUIRE(first != nullptr);
    CHECK(first->code == 1);
    const auto* button = charmap->findMatch("x[C-Up]", 1);
    REQUIRE(button != nullptr);
    CHECK(button->code == 3);
    CHECK(charmap->findMatch("x[C-Up]", 0) == nullptr);
}

TEST_CASE("a malformed charmap is an error naming its line", "[message]") {
    const std::expected<Charmap, std::string> charmap =
        toki::message::parseCharmap("{\n  '[A]' : 0x9F,\n  '[B]' 0xA0,\n}");

    REQUIRE_FALSE(charmap.has_value());
    CHECK(charmap.error().starts_with("line 3"));
}
