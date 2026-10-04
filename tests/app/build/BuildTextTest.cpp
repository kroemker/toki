#include "app/build/domain/AnsiText.h"
#include "app/build/domain/BuildLog.h"
#include "app/build/domain/LineDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using toki::build::AnsiSpan;
using toki::build::Rgb;

TEST_CASE("lines are split wherever the chunks happen to end", "[build][lines]") {
    toki::build::LineDecoder decoder;

    REQUIRE(decoder.decode("make: Ent").empty());
    REQUIRE(decoder.decode("ering dir\r\nCC a.o\nCC") ==
            std::vector<std::string>{"make: Entering dir", "CC a.o"});
    REQUIRE(decoder.finish() == std::optional<std::string>{"CC"});
    REQUIRE_FALSE(decoder.finish().has_value());
}

TEST_CASE("a redrawn line keeps only what was drawn last", "[build][lines]") {
    toki::build::LineDecoder decoder;

    REQUIRE(decoder.decode("10%\r50%\r100% done\n") == std::vector<std::string>{"100% done"});
}

TEST_CASE("bytes that are not UTF-8 are marked rather than dropped", "[build][lines]") {
    REQUIRE(toki::build::makeValidUtf8("caf\xE9!") == "caf\xEF\xBF\xBD!");
    REQUIRE(toki::build::makeValidUtf8("\xC3\xA9") == "\xC3\xA9");
    REQUIRE(toki::build::makeValidUtf8("\xC0\xAF") == "\xEF\xBF\xBD\xEF\xBF\xBD");
    REQUIRE(toki::build::makeValidUtf8("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    REQUIRE(toki::build::makeValidUtf8("\xF0\x9F\x98") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST_CASE("a split UTF-8 character survives the chunk boundary", "[build][lines]") {
    toki::build::LineDecoder decoder;

    REQUIRE(decoder.decode("\xC3").empty());
    REQUIRE(decoder.decode("\xA9\n") == std::vector<std::string>{"\xC3\xA9"});
}

TEST_CASE("colour codes split a line into styled spans", "[build][ansi]") {
    const std::vector<AnsiSpan> spans =
        toki::build::parseAnsiLine("\x1B[01m\x1B[Ka.c:3:\x1B[m \x1B[01;31merror:\x1B[0m bad");

    REQUIRE(spans.size() == 4);
    REQUIRE(spans[0].text == "a.c:3:");
    REQUIRE(spans[0].style.bold);
    REQUIRE_FALSE(spans[0].style.foreground.has_value());
    REQUIRE(spans[1].text == " ");
    REQUIRE_FALSE(spans[1].style.bold);
    REQUIRE(spans[2].text == "error:");
    REQUIRE(spans[2].style.foreground == Rgb{0xCD, 0x31, 0x31});
    REQUIRE(spans[3].text == " bad");
    REQUIRE(spans[3].style == toki::build::AnsiStyle{});
}

TEST_CASE("256-colour and 24-bit codes resolve to their colours", "[build][ansi]") {
    const std::vector<AnsiSpan> spans =
        toki::build::parseAnsiLine("\x1B[38;5;196ma\x1B[48;2;1;2;3mb\x1B[38;5;244mc");

    REQUIRE(spans.size() == 3);
    REQUIRE(spans[0].style.foreground == Rgb{255, 0, 0});
    REQUIRE(spans[1].style.background == Rgb{1, 2, 3});
    REQUIRE(spans[2].style.foreground == Rgb{128, 128, 128});
}

TEST_CASE("other escapes and control characters are dropped", "[build][ansi]") {
    const std::vector<AnsiSpan> spans = toki::build::parseAnsiLine("\x1B[2Ka\x07\tb\x1B");

    REQUIRE(spans.size() == 1);
    REQUIRE(spans[0].text == "a\tb");
}

TEST_CASE("the build log keeps only its newest lines", "[build][log]") {
    toki::build::BuildLog log(2);

    log.append("one");
    log.append("two");
    log.append("three");

    REQUIRE(log.getLines().size() == 2);
    REQUIRE(log.getLines().front() == "two");
    log.clear();
    REQUIRE(log.getLines().empty());
}
