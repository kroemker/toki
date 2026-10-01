#include "hackeroot/ShellWords.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("whitespace separates words", "[hackeroot][shell]") {
    const std::vector<std::string> words = hackeroot::splitShellWords("  -DA  -DB=1\t-std=gnu11 ");

    REQUIRE(words == std::vector<std::string>{"-DA", "-DB=1", "-std=gnu11"});
}

TEST_CASE("quoting is removed and keeps spaces inside one word", "[hackeroot][shell]") {
    const std::vector<std::string> words =
        hackeroot::splitShellWords(R"(-DNAME='"Unknown name"' -DX="a b")");
    const std::vector<std::string> expected = {R"(-DNAME="Unknown name")", "-DX=a b"};

    REQUIRE(words == expected);
}

TEST_CASE("backslashes escape outside quotes and before specials inside them",
          "[hackeroot][shell]") {
    const std::vector<std::string> words = hackeroot::splitShellWords(R"(a\ b "c\"d" "e\f")");
    const std::vector<std::string> expected = {"a b", R"(c"d)", R"(e\f)"};

    REQUIRE(words == expected);
}

TEST_CASE("an empty quoted word is still a word", "[hackeroot][shell]") {
    const std::vector<std::string> words = hackeroot::splitShellWords("-DA='' \"\"");

    REQUIRE(words == std::vector<std::string>{"-DA=", ""});
}
