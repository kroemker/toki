#include "app/scene/domain/SceneView.h"

#include "app/core/domain/Settings.h"
#include "app/scene/domain/ActorMarkers.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <variant>
#include <vector>

using base::s16;
using base::u32;
using base::u8;
using base::usize;

namespace {

/// A room of one triangle at `x`, its display list at the start of segment 3.
std::shared_ptr<const z64::MemoryImage> makeRoomImage(const s16 x) {
    n64::DisplayListWriter writer(0x03);
    const u32 start = writer.addCommand(0xDE01'0000U, 0x0300'0010U);
    writer.endDisplayList();
    const u32 vertices = writer.addVertex({.position = {x, 0, 0}, .color = {255, 0, 0, 255}});
    static_cast<void>(writer.addVertex(
        {.position = {static_cast<s16>(x + 50), 0, 0}, .color = {255, 0, 0, 255}}));
    static_cast<void>(writer.addVertex({.position = {x, 50, 0}, .color = {255, 0, 0, 255}}));
    REQUIRE(writer.getAddress() == 0x0300'0040);
    writer.loadVertices(vertices, 3, 0);
    writer.drawTriangle({0, 1, 2});
    writer.endDisplayList();
    REQUIRE(start == 0x0300'0000);
    auto image = std::make_shared<z64::MemoryImage>();
    image->segments.push_back(z64::ImageSegment{.number = 0x03, .bytes = writer.getBytes()});
    return image;
}

/// A scene of two rooms and nothing of the game's code, in two layers that
/// light it differently.
toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>());
    scene.images.push_back(std::make_shared<const z64::MemoryImage>());
    scene.images.push_back(makeRoomImage(0));
    scene.images.push_back(makeRoomImage(300));

    toki::scene::SceneLayer main;
    main.header.lightSettings.resize(4);
    main.header.lightSettings[1].zFar = 6400;
    main.header.playerEntries.push_back({.position = {0, 0, 100}});
    for (int room = 0; room < 2; ++room) {
        toki::scene::RoomHeader header;
        header.displayLists.push_back({.opaque = 0x0300'0040, .translucent = 0});
        header.actors.push_back({.id = 1, .position = {10, 0, 0}});
        main.rooms.push_back(header);
    }
    toki::scene::SceneLayer night = main;
    night.index = 1;
    night.header.lightSettings.resize(1);
    scene.layers = {main, night};
    return scene;
}

usize countTriangles(const n64::RspResult& result) {
    return static_cast<usize>(
        std::ranges::count_if(result.outputs, [](const n64::RspOutput& output) {
            return std::holds_alternative<n64::Triangle>(output);
        }));
}

toki::viewport::Frame runScene(const toki::scene::Scene& scene,
                               const toki::scene::SceneView& view) {
    return toki::viewport::runFrame(toki::scene::makeSceneContent(scene, view),
                                    toki::viewport::OrbitCamera{}, {.width = 320, .height = 240});
}

} // namespace

TEST_CASE("a scene first shows its main layer at midday, every room, actors marked",
          "[scene][view]") {
    const toki::scene::Scene scene = makeScene();

    const toki::scene::SceneView view = toki::scene::makeSceneView(scene);

    REQUIRE(view.layer == 0);
    REQUIRE(view.lightSetting == 1);
    REQUIRE(view.hiddenRooms == std::vector<bool>{false, false});
    REQUIRE(view.showsActors);
    const toki::viewport::ViewportContent content = toki::scene::makeSceneContent(scene, view);
    REQUIRE(content.lighting.has_value());
    REQUIRE(content.lighting->zFar == 6400.0F);
}

TEST_CASE("every shown room is drawn, and actors are marked by outlined diamonds",
          "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);

    const toki::viewport::Frame all = runScene(scene, view);
    REQUIRE(all.diagnostics.empty());
    const usize marked = countTriangles(all.rspResult) - 2;
    REQUIRE(marked > 3 * 4);
    REQUIRE(marked < 3 * 16);

    view.hiddenRooms[1] = true;
    view.showsActors = false;
    const toki::viewport::Frame one = runScene(scene, view);
    REQUIRE(one.diagnostics.empty());
    REQUIRE(countTriangles(one.rspResult) == 1);
}

TEST_CASE("a view kept across a read again stays within what the scene has", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneView view;
    view.layer = 5;
    view.lightSetting = 3;

    const toki::scene::SceneView kept = toki::scene::keepSceneView(scene, view);

    REQUIRE(kept.layer == 1);
    REQUIRE(kept.lightSetting == 0);
    REQUIRE(kept.hiddenRooms.size() == 2);
}

TEST_CASE("light settings are named by the time of day where it picks them", "[scene][view]") {
    toki::scene::SceneHeader header;

    REQUIRE(toki::scene::makeLightSettingName(header, 1) == "Day");
    REQUIRE(toki::scene::makeLightSettingName(header, 7) == "Night, config 1");
    header.lightMode = toki::scene::LightMode::Settings;
    REQUIRE(toki::scene::makeLightSettingName(header, 7) == "Setting 7");
    REQUIRE(toki::scene::makeLayerName(2) == "Adult day");
    REQUIRE(toki::scene::makeLayerName(5) == "Cutscene 1");
}

TEST_CASE("a session opens the scene again on the same layer of the game", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);
    view.layer = 1;
    view.lightSetting = 0;
    view.hiddenRooms[0] = true;
    view.showsActors = false;

    const toki::core::SceneSession session = toki::scene::makeSceneSession(scene, view);
    REQUIRE(session.layer == 1);
    REQUIRE(session.hiddenRooms == std::vector<usize>{0});

    const toki::scene::SceneView restored = toki::scene::restoreSceneView(scene, session);
    REQUIRE(restored.layer == 1);
    REQUIRE(restored.lightSetting == 0);
    REQUIRE(restored.hiddenRooms == std::vector<bool>{true, false});
    REQUIRE_FALSE(restored.showsActors);
}

TEST_CASE("a session's layer the scene no longer has opens the main one", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    const toki::core::SceneSession session{.layer = 7, .lightSetting = 9, .hiddenRooms = {5}};

    const toki::scene::SceneView restored = toki::scene::restoreSceneView(scene, session);

    REQUIRE(restored.layer == 0);
    REQUIRE(restored.lightSetting == 1);
    REQUIRE(restored.hiddenRooms == std::vector<bool>{false, false});
}
