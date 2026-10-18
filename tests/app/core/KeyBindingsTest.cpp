#include "app/core/domain/KeyBindings.h"

#include <catch2/catch_test_macros.hpp>

using toki::core::Action;
using toki::core::KeyBindings;
using toki::core::KeyChord;
using toki::core::parseKeyChord;

TEST_CASE("a chord reads its modifiers in any order and writes them in one", "[core][keys]") {
    const auto chord = parseKeyChord("shift+CTRL+Z");
    REQUIRE(chord.has_value());
    REQUIRE(*chord == KeyChord{.ctrl = true, .shift = true, .key = "Z"});
    REQUIRE(toki::core::formatKeyChord(*chord) == "Ctrl+Shift+Z");
    REQUIRE(toki::core::formatKeyChord(*parseKeyChord("F5")) == "F5");
    REQUIRE_FALSE(parseKeyChord("")->isBound());
}

TEST_CASE("a chord without a key, or with a modifier twice or unknown, is not read",
          "[core][keys]") {
    REQUIRE_FALSE(parseKeyChord("Ctrl+").has_value());
    REQUIRE_FALSE(parseKeyChord("Ctrl+Ctrl+S").has_value());
    REQUIRE_FALSE(parseKeyChord("Hyper+S").has_value());
}

TEST_CASE("the default bindings have no conflicts, and a rebinding can make one", "[core][keys]") {
    KeyBindings bindings;
    REQUIRE(bindings.findConflicts().empty());
    REQUIRE(bindings.isDefault(Action::MoveTool));

    bindings.setChord(Action::MoveTool, KeyChord{.key = "Q"});
    REQUIRE_FALSE(bindings.isDefault(Action::MoveTool));
    REQUIRE(bindings.findConflicts().size() == 1);
    REQUIRE(bindings.findConflicts()[0].first == Action::SelectTool);
}

TEST_CASE("an action is found by the name the config gives it", "[core][keys]") {
    REQUIRE(toki::core::findAction("move_tool") == Action::MoveTool);
    REQUIRE_FALSE(toki::core::findAction("teleport").has_value());
    for (const toki::core::ActionInfo& info : toki::core::getActions()) {
        REQUIRE(toki::core::findAction(info.id) == info.action);
        REQUIRE(parseKeyChord(info.defaultChord).has_value());
    }
}
