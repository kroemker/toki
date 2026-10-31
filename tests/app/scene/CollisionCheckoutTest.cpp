#include "app/scene/domain/Collision.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneSurface.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using base::s64;
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
            for (const toki::scene::CollisionCamera& camera : collision.cameras) {
                if (!std::ranges::contains(scene.headerNames.cameraSettings, s64{camera.setting},
                                           &toki::scene::NamedValue::value)) {
                    failures.push_back(std::format("{}: camera setting {} has no name", asset.name,
                                                   camera.setting));
                }
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

TEST_CASE("every vanilla scene's collision pairs with its rooms' faces, one face to a polygon",
          "[scene][collision][surface][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize polygons = 0;
    usize paired = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
        const toki::scene::Collision* collision = toki::scene::findLayerCollision(scene, 0);
        if (collision == nullptr) {
            continue;
        }
        const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
        const toki::scene::SceneSurface surface = toki::scene::pairSurface(mesh, *collision);
        polygons += collision->polygons.size();
        std::set<toki::scene::MeshFaceRef> faces;
        for (usize polygon = 0; polygon < surface.polygonFaces.size(); ++polygon) {
            const std::optional<toki::scene::MeshFaceRef>& face = surface.polygonFaces[polygon];
            if (!face) {
                continue;
            }
            ++paired;
            if (!faces.insert(*face).second ||
                toki::scene::findFacePolygon(surface, *face) != polygon) {
                failures.push_back(std::format("{}: polygon {} is paired with room {}'s face {} "
                                               "one way only, or with a face another has",
                                               asset.name, polygon, face->room, face->face));
            }
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n";
    }
    WARN(paired << " of " << polygons << " polygons are a face's");
    INFO(listed);
    CHECK(failures.empty());
}
