#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("every vanilla scene's collision reads without a diagnostic",
          "[scene][collision][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize polygons = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
        if (scene.collisions.empty()) {
            failures.push_back(asset.name + ": no collision");
        }
        for (const toki::scene::SceneLayer& layer : scene.layers) {
            if (layer.header.collisionHeader != 0 && !layer.collision) {
                failures.push_back(asset.name + ": a layer's collision was not read");
            }
        }
        for (const toki::scene::Collision& collision : scene.collisions) {
            polygons += collision.polygons.size();
            if (collision.polygons.empty() || collision.surfaceTypes.empty()) {
                failures.push_back(asset.name + ": an empty collision");
            }
        }
        for (const base::Diagnostic& diagnostic : scene.diagnostics) {
            if (diagnostic.message.contains("collision")) {
                failures.push_back(asset.name + ": " + diagnostic.message);
            }
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n";
    }
    WARN(polygons << " polygons read");
    INFO(listed);
    CHECK(failures.empty());
}
