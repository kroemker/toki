#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneSave.h"
#include "app/scene/domain/SceneSurface.h"
#include "app/scene/domain/SurfaceEdits.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using base::s16;
using base::usize;
using toki::scene::Collision;
using toki::scene::Scene;
using toki::scene::SceneMesh;
using toki::scene::SceneModel;
using toki::scene::SceneSurface;

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

/// The first layer of a scene as the editor reads it: its mesh made solid by
/// the collision it pairs with.
struct ReadSurface {
    SceneMesh mesh;
    Collision collision;
    SceneSurface surface;
};

[[nodiscard]] std::optional<ReadSurface> readSurface(const Scene& scene) {
    const Collision* collision = toki::scene::findLayerCollision(scene, 0);
    if (collision == nullptr) {
        return std::nullopt;
    }
    ReadSurface read{.mesh = toki::scene::readSceneMesh(scene, 0), .collision = *collision};
    read.surface = toki::scene::pairSurface(read.mesh, read.collision);
    read.mesh = toki::scene::makeSolidMesh(std::move(read.mesh), read.surface, read.collision);
    return read;
}

[[nodiscard]] usize countPaired(const SceneSurface& surface) {
    return static_cast<usize>(std::ranges::count_if(
        surface.polygonFaces,
        [](const std::optional<toki::scene::MeshFaceRef>& face) { return face.has_value(); }));
}

[[nodiscard]] Scene showModel(const Scene& scene, const SceneModel& model) {
    Scene shown = scene;
    shown.images = model.layOut();
    toki::scene::readSceneLayers(shown);
    return shown;
}

/// Moves the first welded collision vertex up, and the mesh vertices welded
/// to it with it.
[[nodiscard]] std::expected<void, std::string> moveWelded(const Scene& scene,
                                                          const ReadSurface& read,
                                                          SceneModel& model) {
    const auto welded = std::ranges::find_if(
        read.surface.welds,
        [](const std::vector<toki::scene::MeshVertexRef>& welds) { return !welds.empty(); });
    if (welded == read.surface.welds.end()) {
        return std::unexpected("no collision vertex is welded to the mesh");
    }
    const usize vertex = static_cast<usize>(welded - read.surface.welds.begin());
    std::array<s16, 3> position = read.collision.vertices[vertex];
    position[1] = static_cast<s16>(position[1] + 8);
    const std::array<toki::scene::CollisionVertexMove, 1> collisionMoves{
        {{.vertex = vertex, .position = position}}};
    const std::vector<toki::scene::MeshVertexMove> meshMoves =
        toki::scene::findWeldedMeshMoves(read.surface, collisionMoves);
    if (const std::expected<void, base::Diagnostic> written = toki::scene::applySymbolWrites(
            model, toki::scene::makeMeshMoveWrites(scene, read.mesh, meshMoves));
        !written) {
        return std::unexpected(written.error().message);
    }
    const Collision moved = toki::scene::makeWeldedCollision(read.collision, read.surface,
                                                             read.mesh, meshMoves, collisionMoves);
    if (const std::expected<void, base::Diagnostic> set =
            toki::scene::setCollisionGeometry(model, scene.layers[0].index, moved);
        !set) {
        return std::unexpected(set.error().message);
    }
    return {};
}

/// Takes the first solid face out of its room, and its polygon with it.
[[nodiscard]] std::expected<void, std::string> deleteSolidFace(const Scene& scene,
                                                               const ReadSurface& read,
                                                               SceneModel& model) {
    for (const toki::scene::RoomMesh& room : read.mesh.rooms) {
        const auto solid = std::ranges::find_if(
            room.faces, [](const toki::scene::MeshFace& face) { return face.collision.has_value(); });
        if (solid == room.faces.end()) {
            continue;
        }
        std::vector<toki::scene::RoomDraft> drafts{toki::scene::makeRoomDraft(room)};
        const std::array<usize, 1> deleted{static_cast<usize>(solid - room.faces.begin())};
        toki::scene::deleteFaces(drafts.front(), deleted);
        const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
            toki::scene::planMeshRebuild(scene, model, read.mesh, drafts);
        if (!rebuild) {
            return std::unexpected(rebuild.error().message);
        }
        if (const std::expected<void, base::Diagnostic> applied =
                toki::scene::applyMeshRebuild(model, *rebuild);
            !applied) {
            return std::unexpected(applied.error().message);
        }
        const Collision drafted =
            toki::scene::makeDraftedCollision(read.collision, read.surface, drafts, nullptr);
        if (const std::expected<void, base::Diagnostic> set =
                toki::scene::setCollisionGeometry(model, scene.layers[0].index, drafted);
            !set) {
            return std::unexpected(set.error().message);
        }
        return {};
    }
    return std::unexpected("no face is solid");
}

[[nodiscard]] std::expected<void, std::string>
editSurface(const std::shared_ptr<const hackeroot::Workspace>& workspace, const Scene& scene,
            const ReadSurface& read) {
    SceneModel model(scene);
    if (std::expected<void, std::string> moved = moveWelded(scene, read, model); !moved) {
        return moved;
    }
    const Scene shown = showModel(scene, model);
    const std::optional<ReadSurface> afterMove = readSurface(shown);
    if (!afterMove || countPaired(afterMove->surface) != countPaired(read.surface)) {
        return std::unexpected(std::format(
            "a welded vertex moved, {} polygons pair where {} did",
            afterMove ? countPaired(afterMove->surface) : 0, countPaired(read.surface)));
    }
    if (std::expected<void, std::string> deleted = deleteSolidFace(shown, *afterMove, model);
        !deleted) {
        return deleted;
    }
    const std::optional<ReadSurface> afterDelete = readSurface(showModel(scene, model));
    if (!afterDelete ||
        afterDelete->collision.polygons.size() + 1 != read.collision.polygons.size() ||
        countPaired(afterDelete->surface) + 1 != countPaired(read.surface)) {
        return std::unexpected(std::format(
            "a solid face taken out, {} of {} polygons pair where {} of {} did",
            afterDelete ? countPaired(afterDelete->surface) : 0,
            afterDelete ? afterDelete->collision.polygons.size() : 0, countPaired(read.surface),
            read.collision.polygons.size()));
    }
    const std::expected<hackeroot::EditPlan, std::string> plan = toki::scene::planSceneSave(
        workspace, toki::scene::SceneSaveInput{.name = scene.name,
                                               .directory = scene.directory,
                                               .files = scene.files,
                                               .model = std::move(model)});
    if (!plan) {
        return std::unexpected(plan.error().substr(0, 600));
    }
    return {};
}

} // namespace

TEST_CASE("every vanilla scene with a welded vertex moved and a solid face taken out pairs "
          "as it did, and reads back so",
          "[scene][save][surface][checkout]") {
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
        const Scene scene = toki::scene::readScene(reader, asset);
        if (scene.layers.empty()) {
            continue;
        }
        const std::optional<ReadSurface> read = readSurface(scene);
        if (!read || countPaired(read->surface) == 0) {
            continue;
        }
        if (const std::expected<void, std::string> edited = editSurface(workspace, scene, *read);
            edited) {
            ++saved;
        } else {
            failures.push_back(asset.name + ": " + edited.error());
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n\n";
    }
    WARN(saved << " scenes with their surface edited read back, " << failures.size()
               << " did not");
    INFO(listed);
    CHECK(saved > 0);
    CHECK(failures.empty());
}
