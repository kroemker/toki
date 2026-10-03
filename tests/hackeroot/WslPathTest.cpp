#include "hackeroot/WslPath.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>

TEST_CASE("a path on the WSL share names its distribution and Linux path", "[hackeroot][wsl]") {
    const std::optional<hackeroot::WslPath> path =
        hackeroot::findWslPath(R"(\\wsl.localhost\Ubuntu\home\leo\HackerOoT)");

    REQUIRE(path.has_value());
    REQUIRE(path->distribution == "Ubuntu");
    REQUIRE(path->linuxPath == "/home/leo/HackerOoT");
}

TEST_CASE("the older share name, either slash and any case are accepted", "[hackeroot][wsl]") {
    REQUIRE(hackeroot::findWslPath("//wsl$/Debian/srv/")->linuxPath == "/srv");
    REQUIRE(hackeroot::findWslPath(R"(\\WSL.LOCALHOST\Ubuntu\x\)")->linuxPath == "/x");
    REQUIRE(hackeroot::findWslPath(R"(\\wsl.localhost\Ubuntu)")->linuxPath == "/");
}

TEST_CASE("a path off the WSL share has no WSL path", "[hackeroot][wsl]") {
    REQUIRE_FALSE(hackeroot::findWslPath(R"(C:\Projects\HackerOoT)").has_value());
    REQUIRE_FALSE(hackeroot::findWslPath(R"(\\server\share\HackerOoT)").has_value());
    REQUIRE_FALSE(hackeroot::findWslPath(R"(\\wsl.localhost\)").has_value());
}
