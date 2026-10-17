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

TEST_CASE("a render mode names its shading and each stage it turns off", "[viewport][view]") {
    ViewportView view;
    view.renderMode.shading = toki::viewport::Shading::Normals;
    view.renderMode.wireframe = toki::viewport::Wireframe::OverShading;
    view.renderMode.fog = false;
    view.renderMode.textureFilter = toki::viewport::TextureFilter::Point;

    REQUIRE(listViewChanges(view) ==
            std::vector<std::string>{"Normals", "Wireframe", "No Fog", "Point Filter"});
}
