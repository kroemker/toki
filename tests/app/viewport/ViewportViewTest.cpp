#include "app/viewport/domain/ViewportView.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using toki::viewport::listViewChanges;
using toki::viewport::ViewportView;

TEST_CASE("the view the game would draw names no change", "[viewport][view]") {
    REQUIRE(listViewChanges(ViewportView{}).empty());
}

TEST_CASE("hidden overlays are named as a change", "[viewport][view]") {
    ViewportView view;
    view.overlays.isShown = false;

    REQUIRE(listViewChanges(view) == std::vector<std::string>{"Overlays Off"});
}
