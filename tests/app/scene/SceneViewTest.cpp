#include "app/scene/domain/SceneView.h"

#include "app/core/domain/Settings.h"
#include "app/scene/domain/ActorMarkers.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <tuple>
#include <variant>
#include <vector>

using base::s16;
using base::u32;
using base::u8;
using base::usize;

namespace {

/// A room of one triangle at `x`, its display list at the start of segment 3.
/// Drawn as Jabu-Jabu's rooms are, it first calls segment 8 and multiplies
/// the modelview by the matrix in segment 13, both of which the scene's draw
/// config fills as it runs.
std::shared_ptr<const z64::MemoryImage> makeRoomImage(const s16 x,
                                                      const bool usesRuntimeSegments = false) {
    n64::DisplayListWriter writer(0x03);
    const u32 start = writer.addCommand(0xDE01'0000U, 0x0300'0010U);
    writer.endDisplayList();
    const u32 vertices = writer.addVertex({.position = {x, 0, 0}, .color = {255, 0, 0, 255}});
    static_cast<void>(writer.addVertex(
        {.position = {static_cast<s16>(x + 50), 0, 0}, .color = {255, 0, 0, 255}}));
    static_cast<void>(writer.addVertex({.position = {x, 50, 0}, .color = {255, 0, 0, 255}}));
    REQUIRE(writer.getAddress() == 0x0300'0040);
    if (usesRuntimeSegments) {
        writer.callDisplayList(0x0800'0000);
        writer.addCommand(0xDA38'0001U, 0x0D00'0000U);
    }
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
toki::scene::Scene makeScene(const bool usesRuntimeSegments = false) {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>());
    scene.images.push_back(std::make_shared<const z64::MemoryImage>());
    scene.images.push_back(makeRoomImage(0, usesRuntimeSegments));
    scene.images.push_back(makeRoomImage(300, usesRuntimeSegments));

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

toki::viewport::Frame runScene(const toki::scene::Scene& scene, const toki::scene::SceneView& view,
                               const toki::core::MarkerShape shape) {
    return toki::viewport::runFrame(toki::scene::makeSceneContent(scene, view, shape),
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
    const toki::viewport::ViewportContent content =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(content.lighting.has_value());
    REQUIRE(content.lighting->zFar == 6400.0F);
}

TEST_CASE("every shown room is drawn, and actors are marked by shapes", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);

    const toki::viewport::Frame all = runScene(scene, view, toki::core::MarkerShape::Diamond);
    REQUIRE(all.diagnostics.empty());
    REQUIRE(countTriangles(all.rspResult) > 2 + (3 * 4));

    view.hiddenRooms[1] = true;
    view.showsActors = false;
    const toki::viewport::Frame one = runScene(scene, view, toki::core::MarkerShape::Diamond);
    REQUIRE(one.diagnostics.empty());
    REQUIRE(countTriangles(one.rspResult) == 1);
}

TEST_CASE("every marker shape marks each actor, edges and all", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneView view = toki::scene::makeSceneView(scene);

    constexpr usize TrianglesPerEdge = 12;
    for (const auto [shape, faces, edges] :
         {std::tuple{toki::core::MarkerShape::Cube, usize{12}, usize{12}},
          std::tuple{toki::core::MarkerShape::Diamond, usize{8}, usize{12}},
          std::tuple{toki::core::MarkerShape::Pin, usize{6}, usize{8}}}) {
        const toki::viewport::Frame frame = runScene(scene, view, shape);
        REQUIRE(frame.diagnostics.empty());
        const usize marked = countTriangles(frame.rspResult) - 2;
        REQUIRE(marked > 3 * edges);
        REQUIRE(marked < 3 * (faces + (edges * TrianglesPerEdge)));
    }
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
    view.showsPaths = false;
    view.showsCollision = true;
    view.showsWaterBoxes = false;
    view.collisionColoring = toki::scene::CollisionColoring::SurfaceType;

    const toki::core::SceneSession session = toki::scene::makeSceneSession(scene, view);
    REQUIRE(session.collisionColoring == "surface_type");
    REQUIRE(session.layer == 1);
    REQUIRE(session.hiddenRooms == std::vector<usize>{0});

    const toki::scene::SceneView restored = toki::scene::restoreSceneView(scene, session);
    REQUIRE(restored.layer == 1);
    REQUIRE(restored.lightSetting == 0);
    REQUIRE(restored.hiddenRooms == std::vector<bool>{true, false});
    REQUIRE_FALSE(restored.showsActors);
    REQUIRE_FALSE(restored.showsPaths);
    REQUIRE(restored.showsCollision);
    REQUIRE_FALSE(restored.showsWaterBoxes);
    REQUIRE(restored.collisionColoring == toki::scene::CollisionColoring::SurfaceType);
}

TEST_CASE("a session's layer the scene no longer has opens the main one", "[scene][view]") {
    const toki::scene::Scene scene = makeScene();
    const toki::core::SceneSession session{.layer = 7, .lightSetting = 9, .hiddenRooms = {5}};

    const toki::scene::SceneView restored = toki::scene::restoreSceneView(scene, session);

    REQUIRE(restored.layer == 0);
    REQUIRE(restored.lightSetting == 1);
    REQUIRE(restored.hiddenRooms == std::vector<bool>{false, false});
}

TEST_CASE("segments a scene's draw config fills as it runs neither move a room nor call into "
          "anything",
          "[scene][view]") {
    const toki::scene::Scene plain = makeScene();
    const toki::scene::Scene animated = makeScene(true);
    toki::scene::SceneView view = toki::scene::makeSceneView(plain);
    view.showsActors = false;

    const toki::viewport::Frame expected = runScene(plain, view, toki::core::MarkerShape::Cube);
    const toki::viewport::Frame drawn = runScene(animated, view, toki::core::MarkerShape::Cube);

    REQUIRE(drawn.diagnostics.empty());
    REQUIRE(countTriangles(drawn.rspResult) == 2);
    for (usize i = 0; i < expected.rspResult.outputs.size(); ++i) {
        const auto* want = std::get_if<n64::Triangle>(&expected.rspResult.outputs[i]);
        const auto* got = std::get_if<n64::Triangle>(&drawn.rspResult.outputs[i]);
        if (want == nullptr) {
            continue;
        }
        REQUIRE(got != nullptr);
        for (usize corner = 0; corner < 3; ++corner) {
            REQUIRE(got->vertices[corner].position == want->vertices[corner].position);
        }
    }
}

TEST_CASE("a layer's collision is drawn over the rooms shown, or alone with every room hidden, "
          "every polygon edged",
          "[scene][view][collision]") {
    toki::scene::Scene scene = makeScene();
    toki::scene::Collision collision;
    collision.vertices = {{0, 0, 0}, {0, 0, 100}, {100, 0, 0}, {0, 100, 0}};
    collision.polygons = {{.vertexData = {0, 1, 2}, .normal = {0, 0x7FFF, 0}},
                          {.vertexData = {0, 3, 1}, .normal = {0x7FFF, 0, 0}},
                          {.vertexData = {0, 9, 1}, .normal = {0x7FFF, 0, 0}}};
    collision.surfaceTypes.resize(1);
    scene.collisions.push_back(collision);
    scene.layers[0].collision = 0;
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);
    view.showsActors = false;

    const toki::viewport::ViewportContent off =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(std::ranges::none_of(off.draws, &toki::viewport::ViewportDraw::isEdged));

    view.showsCollision = true;
    const toki::viewport::ViewportContent over =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(std::ranges::count_if(over.draws, &toki::viewport::ViewportDraw::isEdged) == 2);
    REQUIRE(over.draws.back().isEdged);
    REQUIRE(over.labels.back().name == "Collision");
    const toki::viewport::Frame overFrame = runScene(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(overFrame.diagnostics.empty());
    REQUIRE(countTriangles(overFrame.rspResult) == 4);

    std::ranges::fill(view.hiddenRooms, true);
    const toki::viewport::Frame only = runScene(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(only.diagnostics.empty());
    REQUIRE(countTriangles(only.rspResult) == 2);
}

TEST_CASE("waterboxes are shown without the collision, whichever rooms are hidden",
          "[scene][view][collision]") {
    toki::scene::Scene scene = makeScene();
    toki::scene::Collision collision;
    collision.minBounds = {0, -100, 0};
    const auto makeBox = [](const u32 room) {
        return toki::scene::WaterBox{.xMin = 0,
                                     .ySurface = 0,
                                     .zMin = 0,
                                     .xLength = 100,
                                     .zLength = 100,
                                     .properties = room << 13};
    };
    collision.waterBoxes = {makeBox(0), makeBox(1), makeBox(toki::scene::WaterBoxAllRooms)};
    scene.collisions.push_back(collision);
    scene.layers[0].collision = 0;
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);
    view.showsActors = false;

    const toki::viewport::ViewportContent all =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(std::ranges::count_if(all.draws, &toki::viewport::ViewportDraw::isEdged) == 3);
    REQUIRE(runScene(scene, view, toki::core::MarkerShape::Cube).diagnostics.empty());

    std::ranges::fill(view.hiddenRooms, true);
    const toki::viewport::ViewportContent hidden =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(std::ranges::count_if(hidden.draws, &toki::viewport::ViewportDraw::isEdged) == 3);
}

TEST_CASE("collision draws are owned by their polygons and waterboxes, and the rooms by nothing "
          "while collision is shown",
          "[scene][view][collision]") {
    toki::scene::Scene scene = makeScene();
    toki::scene::Collision collision;
    collision.vertices = {{0, 0, 0}, {0, 0, 100}, {100, 0, 0}};
    collision.polygons = {{.type = 1, .vertexData = {0, 1, 2}, .normal = {0, 0x7FFF, 0}}};
    collision.surfaceTypes.resize(2);
    collision.waterBoxes = {
        {.xLength = 100, .zLength = 100, .properties = u32{toki::scene::WaterBoxAllRooms} << 13}};
    scene.collisions.push_back(collision);
    scene.layers[0].collision = 0;
    toki::scene::SceneView view = toki::scene::makeSceneView(scene);
    view.showsActors = false;
    view.showsCollision = true;

    const toki::viewport::ViewportContent content =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    std::vector<toki::scene::SceneNode> owners;
    for (const toki::viewport::ViewportDraw& draw : content.draws) {
        if (draw.owner) {
            owners.push_back(*toki::scene::findNode(*draw.owner));
            REQUIRE_FALSE(draw.isBoxed);
        }
    }
    REQUIRE(owners == std::vector<toki::scene::SceneNode>{
                          toki::scene::CollisionPolygonRef{.layer = 0, .polygon = 0},
                          toki::scene::WaterBoxRef{.layer = 0, .box = 0}});

    view.showsCollision = false;
    view.showsWaterBoxes = false;
    const toki::viewport::ViewportContent hidden =
        toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
    REQUIRE(std::ranges::none_of(hidden.draws, &toki::viewport::ViewportDraw::isEdged));
    REQUIRE(std::ranges::any_of(hidden.draws, [](const toki::viewport::ViewportDraw& draw) {
        return draw.owner &&
               std::holds_alternative<toki::scene::RoomRef>(*toki::scene::findNode(*draw.owner));
    }));

    const toki::scene::SceneNode polygon = toki::scene::CollisionPolygonRef{.polygon = 0};
    REQUIRE(toki::scene::findTintedSurfaceType(scene, view, &polygon) == 1);
    const toki::scene::SceneNode room = toki::scene::RoomRef{};
    REQUIRE_FALSE(toki::scene::findTintedSurfaceType(scene, view, &room));
    view.layer = 1;
    REQUIRE_FALSE(toki::scene::findTintedSurfaceType(scene, view, &polygon));
}
