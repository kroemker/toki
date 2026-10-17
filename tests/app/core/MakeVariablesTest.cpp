#include "app/core/domain/MakeVariables.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("make variables are read off one line of NAME=value words", "[core][settings]") {
    const toki::core::MakeVariables read =
        toki::core::parseMakeVariables("  VERSION=gc-eu-mq\tDEBUG_FEATURES=1 COMPARE= ");

    REQUIRE(read.variables.size() == 3);
    REQUIRE(read.variables.at("VERSION") == "gc-eu-mq");
    REQUIRE(read.variables.at("DEBUG_FEATURES") == "1");
    REQUIRE(read.variables.at("COMPARE").empty());
    REQUIRE(read.rejected.empty());
}

TEST_CASE("a word that is not NAME=value is rejected as written, the rest still read",
          "[core][settings]") {
    const toki::core::MakeVariables read =
        toki::core::parseMakeVariables("VERSION=a stray =1 VERSION=b");

    REQUIRE(read.variables.size() == 1);
    REQUIRE(read.variables.at("VERSION") == "b");
    REQUIRE(read.rejected == std::vector<std::string>{"stray", "=1"});
}

TEST_CASE("make variables are written as one line that reads back the same", "[core][settings]") {
    const toki::core::MakeVariables written =
        toki::core::parseMakeVariables(R"(CFLAGS="-O2 -g" VERSION=gc-eu-mq)");
    REQUIRE(written.variables.at("CFLAGS") == "-O2 -g");

    const std::string line = toki::core::formatMakeVariables(written.variables);

    REQUIRE(line == R"(CFLAGS="-O2 -g" VERSION=gc-eu-mq)");
    REQUIRE(toki::core::parseMakeVariables(line).variables == written.variables);
}
