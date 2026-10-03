#include "cparser/MacroTable.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] cparser::Macro makeMacro(std::string name, std::vector<std::string> parameters = {}) {
    return cparser::Macro{
        .name = std::move(name),
        .parameters = std::move(parameters),
        .functionLike = false,
    };
}

} // namespace

TEST_CASE("a layer answers from its base when it has no definition of its own",
          "[cparser][macros]") {
    cparser::MacroTable base;
    base.define(makeMacro("FROM_BASE"));
    const cparser::MacroTable layer(&base);

    REQUIRE(layer.contains("FROM_BASE"));
    REQUIRE(layer.count() == 0);
}

TEST_CASE("a definition in a layer shadows the base without touching it", "[cparser][macros]") {
    cparser::MacroTable base;
    base.define(makeMacro("SHARED"));
    cparser::MacroTable layer(&base);

    layer.define(makeMacro("SHARED", {"x"}));
    layer.define(makeMacro("ONLY_IN_LAYER"));

    REQUIRE(layer.find("SHARED")->parameters.size() == 1);
    REQUIRE(base.find("SHARED")->parameters.empty());
    REQUIRE_FALSE(base.contains("ONLY_IN_LAYER"));
}

TEST_CASE("an undef in a layer hides the base's definition from that layer only",
          "[cparser][macros]") {
    cparser::MacroTable base;
    base.define(makeMacro("GUARD_H"));
    cparser::MacroTable layer(&base);

    layer.remove("GUARD_H");

    REQUIRE_FALSE(layer.contains("GUARD_H"));
    REQUIRE(base.contains("GUARD_H"));

    SECTION("and a later define brings the name back") {
        layer.define(makeMacro("GUARD_H", {"y"}));

        REQUIRE(layer.find("GUARD_H")->parameters.size() == 1);
    }
}

TEST_CASE("defineIfAbsent counts a base definition as present", "[cparser][macros]") {
    cparser::MacroTable base;
    base.define(makeMacro("FIRST"));
    cparser::MacroTable layer(&base);

    layer.defineIfAbsent(makeMacro("FIRST", {"x"}));

    REQUIRE(layer.find("FIRST")->parameters.empty());
    REQUIRE(layer.count() == 0);
}

TEST_CASE("layers stack", "[cparser][macros]") {
    cparser::MacroTable scraped;
    scraped.define(makeMacro("DEEP"));
    cparser::MacroTable config(&scraped);
    config.define(makeMacro("MIDDLE"));
    cparser::MacroTable unit(&config);
    unit.remove("DEEP");

    REQUIRE(unit.contains("MIDDLE"));
    REQUIRE_FALSE(unit.contains("DEEP"));
    REQUIRE(config.contains("DEEP"));
}

TEST_CASE("a layer lists only the names it defines itself", "[cparser][macros]") {
    cparser::MacroTable base;
    base.define(makeMacro("BELOW"));
    cparser::MacroTable layer(&base);
    layer.define(makeMacro("HERE"));

    REQUIRE(layer.getDefinedNames() == std::vector<std::string_view>{"HERE"});
}
