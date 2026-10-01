#include "z64/MacroTable.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] z64::Macro macroNamed(std::string name, std::vector<std::string> parameters = {}) {
    return z64::Macro{
        .name = std::move(name),
        .parameters = std::move(parameters),
        .functionLike = false,
    };
}

} // namespace

TEST_CASE("a layer answers from its base when it has no definition of its own", "[z64][macros]") {
    z64::MacroTable base;
    base.define(macroNamed("FROM_BASE"));
    const z64::MacroTable layer(&base);

    REQUIRE(layer.contains("FROM_BASE"));
    REQUIRE(layer.count() == 0);
}

TEST_CASE("a definition in a layer shadows the base without touching it", "[z64][macros]") {
    z64::MacroTable base;
    base.define(macroNamed("SHARED"));
    z64::MacroTable layer(&base);

    layer.define(macroNamed("SHARED", {"x"}));
    layer.define(macroNamed("ONLY_IN_LAYER"));

    REQUIRE(layer.find("SHARED")->parameters.size() == 1);
    REQUIRE(base.find("SHARED")->parameters.empty());
    REQUIRE_FALSE(base.contains("ONLY_IN_LAYER"));
}

TEST_CASE("an undef in a layer hides the base's definition from that layer only", "[z64][macros]") {
    z64::MacroTable base;
    base.define(macroNamed("GUARD_H"));
    z64::MacroTable layer(&base);

    layer.remove("GUARD_H");

    REQUIRE_FALSE(layer.contains("GUARD_H"));
    REQUIRE(base.contains("GUARD_H"));

    SECTION("and a later define brings the name back") {
        layer.define(macroNamed("GUARD_H", {"y"}));

        REQUIRE(layer.find("GUARD_H")->parameters.size() == 1);
    }
}

TEST_CASE("defineIfAbsent counts a base definition as present", "[z64][macros]") {
    z64::MacroTable base;
    base.define(macroNamed("FIRST"));
    z64::MacroTable layer(&base);

    layer.defineIfAbsent(macroNamed("FIRST", {"x"}));

    REQUIRE(layer.find("FIRST")->parameters.empty());
    REQUIRE(layer.count() == 0);
}

TEST_CASE("layers stack", "[z64][macros]") {
    z64::MacroTable scraped;
    scraped.define(macroNamed("DEEP"));
    z64::MacroTable config(&scraped);
    config.define(macroNamed("MIDDLE"));
    z64::MacroTable unit(&config);
    unit.remove("DEEP");

    REQUIRE(unit.contains("MIDDLE"));
    REQUIRE_FALSE(unit.contains("DEEP"));
    REQUIRE(config.contains("DEEP"));
}

TEST_CASE("a layer lists only the names it defines itself", "[z64][macros]") {
    z64::MacroTable base;
    base.define(macroNamed("BELOW"));
    z64::MacroTable layer(&base);
    layer.define(macroNamed("HERE"));

    REQUIRE(layer.definedNames() == std::vector<std::string_view>{"HERE"});
}
