#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "n64/RspResult.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

using base::usize;

namespace {

std::optional<hackeroot::Workspace> loadCheckout() {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        return std::nullopt;
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    return std::move(*workspace);
}

usize countTriangles(const n64::RspResult& result) {
    return static_cast<usize>(
        std::ranges::count_if(result.outputs, [](const n64::RspOutput& output) {
            return std::holds_alternative<n64::Triangle>(output);
        }));
}

std::string describe(const std::vector<base::Diagnostic>& diagnostics) {
    std::map<std::string, usize> kinds;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        ++kinds[diagnostic.message];
    }
    std::string described;
    for (const auto& [message, count] : kinds) {
        if (described.size() < 4000) {
            described += std::to_string(count) + "x " + message + "\n";
        }
    }
    return described;
}

} // namespace

TEST_CASE("a vanilla and a fast64 scene read and draw every room", "[scene][checkout]") {
    std::optional<hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto shared = std::make_shared<const hackeroot::Workspace>(std::move(*workspace));
    for (const std::string name : {"spot04", "ydan", "example"}) {
        const auto& scenes = shared->getAssets().scenes;
        const auto asset = std::ranges::find(scenes, name, &hackeroot::AssetDirectory::name);
        REQUIRE(asset != scenes.end());

        hackeroot::AssetImageReader reader(shared);
        toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
        scene.actors = toki::scene::readSceneActors(reader, scene.layers);
        const toki::scene::Scene again = toki::scene::readScene(reader, *asset);
        CHECK(again.layers.size() == scene.layers.size());
        INFO(name << ": " << describe(scene.diagnostics));
        CHECK(std::ranges::none_of(scene.diagnostics, [](const base::Diagnostic& diagnostic) {
            return diagnostic.severity == base::Severity::Error;
        }));
        REQUIRE_FALSE(scene.layers.empty());
        const toki::scene::SceneLayer& main = scene.layers.front();
        REQUIRE(main.rooms.size() == asset->roomCount);
        REQUIRE(main.header.roomCount == main.rooms.size());
        REQUIRE_FALSE(main.header.playerEntries.empty());
        REQUIRE_FALSE(main.header.lightSettings.empty());
        for (const toki::scene::RoomHeader& room : main.rooms) {
            CHECK_FALSE(room.displayLists.empty());
        }
        CHECK(main.header.keepObjectId != 0);
        CHECK(std::ranges::any_of(
            main.rooms, [](const toki::scene::RoomHeader& room) { return !room.objects.empty(); }));

        const toki::viewport::ViewportContent content =
            toki::scene::makeSceneContent(scene, toki::scene::makeSceneView(scene));
        const toki::viewport::Frame frame = toki::viewport::runFrame(
            content, toki::viewport::OrbitCamera{}, {.width = 640, .height = 480});
        INFO("frame: " << describe(frame.diagnostics));
        WARN(name << ": " << main.rooms.size() << " rooms, " << scene.layers.size() << " layers, "
                  << content.draws.size() << " draws, " << countTriangles(frame.rspResult)
                  << " triangles, " << frame.diagnostics.size() << " frame diagnostics, read in "
                  << scene.elapsed.count() << " ms, again in " << again.elapsed.count() << " ms");
        CHECK(countTriangles(frame.rspResult) > 100);
        const auto drawn = std::ranges::count_if(scene.actors.models, [](const auto& entry) {
            return entry.second.drawing.has_value();
        });
        WARN(name << ": " << drawn << " of " << scene.actors.models.size()
                  << " actors drawn as models, from " << scene.actors.objects.size()
                  << " objects, read in " << scene.actors.elapsed.count() << " ms");
        if (toki::scene::hasAnimatedActors(scene.actors)) {
            toki::scene::SceneView playing = toki::scene::makeSceneView(scene);
            playing.actorFrame = 7;
            const toki::viewport::Frame played = toki::viewport::runFrame(
                toki::scene::makeSceneContent(scene, playing), toki::viewport::OrbitCamera{},
                {.width = 640, .height = 480});
            CHECK(played.diagnostics.size() == frame.diagnostics.size());
            CHECK(countTriangles(played.rspResult) > 100);
            const toki::viewport::ViewportContent moved =
                toki::scene::makeSceneContent(scene, playing);
            REQUIRE(moved.images.size() == content.images.size());
            const bool isAnyMoved = std::ranges::any_of(
                std::views::iota(base::usize{0}, moved.images.size()), [&](const base::usize i) {
                    return moved.images[i]->segments.front().bytes !=
                           content.images[i]->segments.front().bytes;
                });
            if (name == "ydan") {
                CHECK(isAnyMoved);
            }
        }
        if (name == "ydan") {
            // Its draw config points segment 8 at the entrance's day or night
            // texture, which the default draw config the viewport stands in
            // with cannot: one load reads past gEmptyDL until draw configs run.
            // Obj_Oshihiki points it at a texture of a table no object
            // declares, which is a gap: its load reads past gEmptyDL too.
            CHECK(frame.diagnostics.size() <= 2);
        } else {
            CHECK(frame.diagnostics.empty());
        }
    }
}

TEST_CASE("how long a scene's actors take to read", "[.measure][scene][checkout]") {
    std::optional<hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto shared = std::make_shared<const hackeroot::Workspace>(std::move(*workspace));
    const auto& scenes = shared->getAssets().scenes;
    const auto asset =
        std::ranges::find(scenes, std::string("ice_doukutu"), &hackeroot::AssetDirectory::name);
    REQUIRE(asset != scenes.end());

    hackeroot::AssetImageReader reader(shared);
    const toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
    hackeroot::AssetImageReader actorReader(shared);
    for (const char* read : {"first", "again"}) {
        const toki::scene::SceneActors actors =
            toki::scene::readSceneActors(actorReader, scene.layers);
        const auto drawn = std::ranges::count_if(
            actors.models, [](const auto& entry) { return entry.second.drawing.has_value(); });
        WARN("ice_doukutu, " << read << ": scene in " << scene.elapsed.count() << " ms, actors in "
                             << actors.elapsed.count() << " ms (" << actors.overlaysElapsed.count()
                             << " ms overlays, " << actors.objectsElapsed.count()
                             << " ms objects), " << drawn << " of " << actors.models.size()
                             << " drawn, " << actors.objects.size() << " objects");
    }
}
