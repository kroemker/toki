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
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
