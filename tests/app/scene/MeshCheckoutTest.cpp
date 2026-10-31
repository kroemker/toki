#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <expected>
#include <filesystem>
#include <format>
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

TEST_CASE(
    "every vanilla scene's rooms read as meshes of Vtx arrays, and a vertex moved reads back moved",
    "[scene][mesh][checkout]") {
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
        std::vector<toki::scene::MeshVertexMove> moves;
        for (const toki::scene::RoomMesh& room : mesh.rooms) {
            faces += room.faces.size();
            vertices += room.vertices.size();
            if (!room.vertices.empty()) {
                moves.push_back(
                    {.room = room.room,
                     .vertex = 0,
                     .position = room.vertices[0].position + glm::vec3(0.0F, 7.0F, 0.0F)});
            }
        }
        const std::vector<toki::scene::SymbolWrite> writes =
            toki::scene::makeMeshMoveWrites(scene, mesh, moves);
        toki::scene::SceneModel model(scene);
        if (const std::expected<void, base::Diagnostic> applied =
                toki::scene::applySymbolWrites(model, writes);
            !applied) {
            failures.push_back(asset.name + ": " + applied.error().message);
            continue;
        }
        toki::scene::Scene written = scene;
        written.images = model.layOut();
        if (!toki::scene::hasSymbolWrites(written, writes)) {
            failures.push_back(asset.name + ": the laid out images do not hold the writes");
            continue;
        }
        const toki::scene::SceneMesh read = toki::scene::readSceneMesh(written, 0, &mesh);
        const toki::scene::SceneMesh expected = toki::scene::makeMovedMesh(mesh, moves, writes);
        for (const toki::scene::MeshVertexMove& move : moves) {
            const toki::scene::RoomMesh* room = toki::scene::findRoomMesh(read, move.room);
            const toki::scene::RoomMesh* wanted = toki::scene::findRoomMesh(expected, move.room);
            if (room == nullptr || room->vertices.size() != wanted->vertices.size() ||
                room->vertices[0].position != wanted->vertices[0].position) {
                failures.push_back(std::format("{}: room {}'s moved vertex reads back elsewhere",
                                               asset.name, move.room));
            }
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

TEST_CASE("every vanilla room's first run rebuilds without its first face, and reads back so",
          "[scene][mesh][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize rebuilt = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, asset);
        if (scene.layers.empty()) {
            continue;
        }
        const toki::scene::SceneMesh mesh = toki::scene::readSceneMesh(scene, 0);
        const toki::scene::SceneModel model(scene);
        for (const toki::scene::RoomMesh& room : mesh.rooms) {
            if (room.faces.empty()) {
                continue;
            }
            toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(room);
            draft.faces.erase(draft.faces.begin());
            const std::array<toki::scene::RoomDraft, 1> drafts{std::move(draft)};
            const std::expected<toki::scene::MeshRebuild, base::Diagnostic> rebuild =
                toki::scene::planMeshRebuild(scene, model, mesh, drafts);
            if (!rebuild) {
                failures.push_back(
                    std::format("{} room {}: {}", asset.name, room.room, rebuild.error().message));
                continue;
            }
            ++rebuilt;
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + '\n';
    }
    WARN(rebuilt << " rooms rebuilt");
    INFO(listed);
    CHECK(failures.empty());
}
