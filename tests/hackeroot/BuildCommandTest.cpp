#include "hackeroot/BuildCommand.h"

#include "makeparser/Makefile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using hackeroot::BuildTarget;

TEST_CASE("a checkout on a WSL share builds in its own distribution", "[hackeroot][build]") {
    const std::vector<std::string> command = hackeroot::makeBuildCommand(
        R"(\\wsl.localhost\Ubuntu\home\leo\HackerOoT)", {}, BuildTarget::Build, 8);

    REQUIRE(command == std::vector<std::string>{"wsl.exe", "--distribution", "Ubuntu", "--cd",
                                                "/home/leo/HackerOoT", "--exec", "make", "-j8"});
}

TEST_CASE("a checkout elsewhere builds in the default distribution", "[hackeroot][build]") {
    const std::vector<std::string> command = hackeroot::makeBuildCommand(
        std::filesystem::path(R"(D:\mods\HackerOoT)"), {}, BuildTarget::Build, 4);

    REQUIRE(command == std::vector<std::string>{"wsl.exe", "--cd", R"(D:\mods\HackerOoT)", "--exec",
                                                "make", "-j4"});
}

TEST_CASE("build settings become make's command line, and run comes last", "[hackeroot][build]") {
    const makeparser::MakeOverrides settings{{"VERSION", "gc-eu-mq"}, {"DEBUG_FEATURES", "0"}};

    const std::vector<std::string> command =
        hackeroot::makeBuildCommand("//wsl$/Debian/src/oot", settings, BuildTarget::Run, 0);

    REQUIRE(command == std::vector<std::string>{"wsl.exe", "--distribution", "Debian", "--cd",
                                                "/src/oot", "--exec", "make", "-j1",
                                                "DEBUG_FEATURES=0", "VERSION=gc-eu-mq", "run"});
}
