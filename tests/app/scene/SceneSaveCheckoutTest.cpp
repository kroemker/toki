#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/HeaderSettings.h"
#include "app/scene/domain/LightSettingEdits.h"
#include "app/scene/domain/PathEdits.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneSave.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using base::usize;

namespace {

std::shared_ptr<const hackeroot::Workspace> loadCheckout() {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        return nullptr;
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    if (const std::optional<base::Diagnostic> unsupported =
            hackeroot::findUnsupportedMicrocode(*workspace)) {
        SKIP(unsupported->message);
    }
    return std::make_shared<const hackeroot::Workspace>(std::move(*workspace));
}

std::expected<hackeroot::EditPlan, std::string>
planUnchangedSave(const std::shared_ptr<const hackeroot::Workspace>& workspace,
                  const hackeroot::AssetDirectory& asset) {
    hackeroot::AssetImageReader reader(workspace);
    const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
    return toki::scene::planSceneSave(
        workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                               .directory = scene.directory,
                                               .files = scene.files,
                                               .model = toki::scene::SceneModel(scene)});
}

std::expected<void, base::Diagnostic> editCollision(toki::scene::SceneModel& model,
                                                    const toki::scene::Scene& scene) {
    using toki::scene::PolygonFlag;
    const toki::scene::Collision& collision = *toki::scene::findLayerCollision(scene, 0);
    const usize layer = scene.layers.front().index;
    if (scene.layers.size() > 1 && scene.layers[1].collision == scene.layers[0].collision) {
        if (std::expected<void, base::Diagnostic> owned =
                toki::scene::makeOwnCollision(model, scene.layers[1].index);
            !owned) {
            return owned;
        }
    }
    const std::array<usize, 1> first{0};
    toki::scene::SurfaceFields fields =
        toki::scene::readSurfaceFields(collision.surfaceTypes[collision.polygons[0].type]);
    fields.material = static_cast<base::u8>((fields.material + 1) & 0x0F);
    const std::expected<usize, base::Diagnostic> added =
        toki::scene::addSurfaceType(model, layer, toki::scene::makeSurfaceType(fields));
    if (!added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set =
            toki::scene::setPolygonSurfaceType(model, layer, first, static_cast<base::u16>(*added));
        !set) {
        return set;
    }
    if (std::expected<void, base::Diagnostic> flagged =
            toki::scene::setPolygonFlag(model, layer, first, PolygonFlag::IgnoreCamera, true);
        !flagged) {
        return flagged;
    }
    const toki::scene::WaterBox box{.xMin = -50,
                                    .ySurface = 20,
                                    .zMin = -50,
                                    .xLength = 100,
                                    .zLength = 100,
                                    .properties = toki::scene::makeWaterBoxProperties(
                                        0, 0, toki::scene::WaterBoxAllRooms, false)};
    if (const std::expected<usize, base::Diagnostic> water =
            toki::scene::addWaterBox(model, layer, box);
        !water) {
        return std::unexpected(water.error());
    }
    if (const std::expected<usize, base::Diagnostic> removed =
            toki::scene::removeUnusedSurfaceTypes(model, layer);
        !removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

} // namespace

TEST_CASE("a vanilla scene saved without an edit is copied out and reads back the same",
          "[scene][save][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto& scenes = workspace->getAssets().scenes;
    const auto asset = std::ranges::find_if(scenes, [](const hackeroot::AssetDirectory& scene) {
        return scene.name == "spot04" && scene.isExtracted;
    });
    REQUIRE(asset != scenes.end());

    const std::expected<hackeroot::EditPlan, std::string> plan =
        planUnchangedSave(workspace, *asset);

    INFO((plan ? std::string() : plan.error()));
    REQUIRE(plan.has_value());
    REQUIRE(std::ranges::none_of(plan->files, &hackeroot::FileEdit::isRemoved));
    REQUIRE(std::ranges::any_of(plan->files, [](const hackeroot::FileEdit& edit) {
        return edit.file.generic_string() == "assets/scenes/overworld/spot04/spot04_scene_main.c";
    }));
    REQUIRE(std::ranges::any_of(plan->files, [](const hackeroot::FileEdit& edit) {
        return edit.file.generic_string().starts_with("spec/");
    }));
    REQUIRE_FALSE(plan->copies.empty());
    const auto collision = std::ranges::find_if(plan->files, [](const hackeroot::FileEdit& edit) {
        return edit.file.filename() == "spot04_scene_col.c";
    });
    REQUIRE(collision != plan->files.end());
    for (const std::string_view macro : {"COLPOLY_VTX(", "COLPOLY_SNORMAL(", "SURFACETYPE0(",
                                         "WATERBOX_PROPERTIES(", "CAM_SET_"}) {
        INFO(macro);
        CHECK(collision->after.contains(macro));
    }
}

TEST_CASE("every vanilla scene saved without an edit reads back the same",
          "[scene][save][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize saved = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        const std::expected<hackeroot::EditPlan, std::string> plan =
            planUnchangedSave(workspace, asset);
        if (plan) {
            ++saved;
        } else {
            failures.push_back(asset.name + ": " + plan.error().substr(0, 600));
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n\n";
    }
    WARN(saved << " scenes read back the same, " << failures.size() << " did not, in "
               << std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - started)
                      .count()
               << " s");
    INFO(listed);
    CHECK(failures.empty());
}

TEST_CASE("a vanilla scene with its actors edited reads back as edited",
          "[scene][save][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto& scenes = workspace->getAssets().scenes;
    const auto asset =
        std::ranges::find(scenes, std::string("spot04"), &hackeroot::AssetDirectory::name);
    REQUIRE(asset != scenes.end());
    hackeroot::AssetImageReader reader(workspace);
    const toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
    REQUIRE(scene.layers.size() > 2);
    toki::scene::SceneModel model(scene);
    const toki::scene::PlacementList adult{.kind = toki::scene::PlacementKind::Actor, .layer = 2};
    toki::scene::ActorPlacement moved = scene.layers[2].rooms[0].actors.front();
    moved.position[1] = static_cast<base::s16>(moved.position[1] + 100);

    REQUIRE(toki::scene::setPlacement(model, adult, 0, moved));
    REQUIRE(toki::scene::addPlacement(model, adult, moved));
    REQUIRE(toki::scene::removePlacement(model, {.kind = toki::scene::PlacementKind::Actor}, 0));
    const std::vector<base::s16> crowded(20, base::s16{0x0003});
    REQUIRE(toki::scene::addRoomObjects(model, 0, 1, crowded));
    const std::expected<hackeroot::EditPlan, std::string> plan = toki::scene::planSceneSave(
        workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                               .directory = scene.directory,
                                               .files = scene.files,
                                               .model = std::move(model)});

    INFO((plan ? std::string() : plan.error()));
    REQUIRE(plan.has_value());
    REQUIRE(std::ranges::any_of(plan->warnings, [](const std::string& warning) {
        return warning.starts_with("Room 1 lists") && warning.contains("slots");
    }));
}

TEST_CASE("a vanilla scene with its header settings edited reads back as edited",
          "[scene][save][checkout]") {
    using toki::scene::HeaderSetting;
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto& scenes = workspace->getAssets().scenes;
    const auto asset =
        std::ranges::find(scenes, std::string("spot04"), &hackeroot::AssetDirectory::name);
    REQUIRE(asset != scenes.end());
    hackeroot::AssetImageReader reader(workspace);
    const toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
    toki::scene::SceneModel model(scene);

    REQUIRE(toki::scene::setHeaderSetting(model, {}, HeaderSetting::Sequence, 0x7F));
    REQUIRE(toki::scene::setHeaderSetting(model, {.layer = 2}, HeaderSetting::Skybox, 0));
    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::LensMode, 1));
    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 1}, HeaderSetting::WindStrength, 30));
    REQUIRE(toki::scene::setRoomObjects(model, 2, 1, {}));
    const std::expected<hackeroot::EditPlan, std::string> plan = toki::scene::planSceneSave(
        workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                               .directory = scene.directory,
                                               .files = scene.files,
                                               .model = std::move(model)});

    INFO((plan ? std::string() : plan.error()));
    REQUIRE(plan.has_value());
}

TEST_CASE("a vanilla scene with its light settings and paths edited reads back as edited",
          "[scene][save][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const auto& scenes = workspace->getAssets().scenes;
    const auto asset =
        std::ranges::find(scenes, std::string("spot04"), &hackeroot::AssetDirectory::name);
    REQUIRE(asset != scenes.end());
    hackeroot::AssetImageReader reader(workspace);
    const toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
    REQUIRE(scene.layers.size() > 2);
    REQUIRE_FALSE(scene.layers[2].header.paths.empty());
    toki::scene::SceneModel model(scene);
    toki::scene::LightSetting dusk = scene.layers[2].header.lightSettings.front();
    dusk.fogNear = 900;
    dusk.blendRate = 5;
    std::vector<std::array<base::s16, 3>> points = scene.layers[2].header.paths.front().points;
    points.push_back({0, 100, 0});

    REQUIRE(toki::scene::setLightSetting(model, 2, 0, dusk));
    REQUIRE(toki::scene::addLightSetting(model, 0, dusk));
    REQUIRE(toki::scene::setPathPoints(model, 2, 0, points));
    REQUIRE(toki::scene::addPath(model, 0, points));
    REQUIRE(toki::scene::removePath(model, 0, 0));
    const std::expected<hackeroot::EditPlan, std::string> plan = toki::scene::planSceneSave(
        workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                               .directory = scene.directory,
                                               .files = scene.files,
                                               .model = std::move(model)});

    INFO((plan ? std::string() : plan.error()));
    REQUIRE(plan.has_value());
}

TEST_CASE("every vanilla scene with its collision edited reads back as edited",
          "[scene][save][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize saved = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
        const toki::scene::Collision* collision = toki::scene::findLayerCollision(scene, 0);
        if (collision == nullptr || collision->polygons.empty()) {
            continue;
        }
        toki::scene::SceneModel model(scene);
        if (const std::expected<void, base::Diagnostic> edited = editCollision(model, scene);
            !edited) {
            failures.push_back(asset.name + ": " + edited.error().message);
            continue;
        }
        const std::expected<hackeroot::EditPlan, std::string> plan = toki::scene::planSceneSave(
            workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                                   .directory = scene.directory,
                                                   .files = scene.files,
                                                   .model = std::move(model)});
        if (plan) {
            ++saved;
        } else {
            failures.push_back(asset.name + ": " + plan.error().substr(0, 600));
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n\n";
    }
    WARN(saved << " scenes with their collision edited read back, " << failures.size()
               << " did not");
    INFO(listed);
    CHECK(saved > 0);
    CHECK(failures.empty());
}
