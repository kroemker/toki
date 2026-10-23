#include "app/scene/domain/SceneSelection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using base::usize;
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

TEST_CASE("a node is the same node again from its key", "[scene][selection]") {
    for (const SceneNode& node :
         {SceneNode{PlacementRef{
              .kind = toki::scene::PlacementKind::TransitionActor, .layer = 2, .index = 7}},
          SceneNode{PlacementRef{.layer = 1, .room = 3, .index = 40}}, Room,
          SceneNode{toki::scene::LightSettingRef{.layer = 1, .index = 9}},
          SceneNode{toki::scene::PathRef{.layer = 3, .path = 2}},
          SceneNode{toki::scene::PathPointRef{.layer = 3, .path = 2, .point = 17}}}) {
        REQUIRE(toki::scene::findNode(toki::scene::makeNodeKey(node)) == node);
    }
    REQUIRE_FALSE(toki::scene::findNode(usize{9} << 56U));
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

TEST_CASE("what a transform moves is the placements and path points selected",
          "[scene][selection]") {
    SceneSelection selection;
    const SceneNode point = toki::scene::PathPointRef{.path = 1, .point = 2};
    const std::array<SceneNode, 4> picked{Room, First, toki::scene::PathRef{.path = 1}, point};
    selection.pick(picked, PickMode::Replace);

    REQUIRE(selection.listMovable() == std::vector<SceneNode>{First, point});
}
