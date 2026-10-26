#include "app/scene/domain/RoomMesh.h"
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

TEST_CASE("every vanilla scene's rooms read as meshes of Vtx arrays", "[scene][mesh][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize faces = 0;
    usize vertices = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
        if (scene.layers.empty()) {
            failures.push_back(asset.name + ": no layer");
            continue;
        }
        const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
        for (const base::Diagnostic& diagnostic : mesh.diagnostics) {
            failures.push_back(asset.name + ": " + diagnostic.message);
        }
        for (const toki::scene::RoomMesh& room : mesh.rooms) {
            faces += room.faces.size();
            vertices += room.vertices.size();
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n";
    }
    WARN(faces << " faces over " << vertices << " vertices read");
    INFO(listed);
    CHECK(failures.empty());
}
