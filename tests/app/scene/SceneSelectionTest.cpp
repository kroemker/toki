#include "app/scene/domain/SceneSelection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using toki::scene::PickMode;
using toki::scene::PlacementRef;
using toki::scene::RoomRef;
using toki::scene::SceneNode;
using toki::scene::SceneSelection;

namespace {

const SceneNode First = PlacementRef{.index = 0};
const SceneNode Second = PlacementRef{.index = 1};
const SceneNode Room = RoomRef{.room = 2};

} // namespace

TEST_CASE("a pick replaces the selection, or adds to, takes from or toggles it",
          "[scene][selection]") {
    SceneSelection selection;
    const std::array<SceneNode, 2> both{First, Second};
    selection.pick(both, PickMode::Replace);
    REQUIRE(selection.getNodes().size() == 2);
    REQUIRE(*selection.findActive() == Second);

    const std::array<SceneNode, 1> first{First};
    selection.pick(first, PickMode::Toggle);
    REQUIRE_FALSE(selection.contains(First));
    selection.pick(first, PickMode::Toggle);
    REQUIRE(*selection.findActive() == First);

    selection.pick(first, PickMode::Add);
    REQUIRE(selection.getNodes().size() == 2);
    selection.pick(first, PickMode::Remove);
    REQUIRE(selection.getNodes().size() == 1);

    selection.pick({}, PickMode::Replace);
    REQUIRE(selection.isEmpty());
}

TEST_CASE("the placements of a selection leave its rooms out", "[scene][selection]") {
    SceneSelection selection;
    const std::array<SceneNode, 3> picked{Room, First, Second};
    selection.pick(picked, PickMode::Replace);
    REQUIRE(selection.listPlacements().size() == 2);

    selection.removeIf([](const SceneNode& node) { return node == Second; });
    REQUIRE(selection.listPlacements().size() == 1);
    REQUIRE(selection.contains(Room));
}
