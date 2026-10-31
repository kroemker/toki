#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/MeshOperations.h"
#include "app/scene/domain/MeshRebuild.h"
#include "app/scene/domain/MeshShapes.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneMaterials.h"
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
#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
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
    INFO((workspace ? std::string() : workspace.error().message));
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

[[nodiscard]] std::optional<ReadSurface> readSurface(const Scene& scene,
                                                     std::span<const SceneMesh* const> kept) {
    const Collision* collision = toki::scene::findLayerCollision(scene, 0);
    if (collision == nullptr) {
        return std::nullopt;
    }
    ReadSurface read{.mesh = toki::scene::readSceneMesh(scene, 0, kept), .collision = *collision};
    read.surface = toki::scene::pairSurface(read.mesh, read.collision);
    read.mesh = toki::scene::makeSolidMesh(std::move(read.mesh), read.surface, read.collision);
    return read;
}

[[nodiscard]] std::optional<ReadSurface> readSurface(const Scene& scene,
                                                     const SceneMesh* kept = nullptr) {
    if (kept == nullptr) {
        return readSurface(scene, std::span<const SceneMesh* const>{});
    }
    const std::array<const SceneMesh*, 1> candidates{kept};
    return readSurface(scene, candidates);
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
[[nodiscard]] std::expected<void, std::string>
moveWelded(const Scene& scene, const ReadSurface& read, SceneModel& model) {
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
[[nodiscard]] std::expected<void, std::string>
deleteSolidFace(const Scene& scene, const ReadSurface& read, SceneModel& model) {
    for (const toki::scene::RoomMesh& room : read.mesh.rooms) {
        const auto solid = std::ranges::find_if(room.faces, [](const toki::scene::MeshFace& face) {
            return face.collision.has_value();
        });
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
        return std::unexpected(std::format("a welded vertex moved, {} polygons pair where {} did",
                                           afterMove ? countPaired(afterMove->surface) : 0,
                                           countPaired(read.surface)));
    }
    if (std::expected<void, std::string> deleted = deleteSolidFace(shown, *afterMove, model);
        !deleted) {
        return deleted;
    }
    const std::optional<ReadSurface> afterDelete = readSurface(showModel(scene, model));
    if (!afterDelete ||
        afterDelete->collision.polygons.size() + 1 != read.collision.polygons.size() ||
        countPaired(afterDelete->surface) + 1 != countPaired(read.surface)) {
        return std::unexpected(
            std::format("a solid face taken out, {} of {} polygons pair where {} of {} did",
                        afterDelete ? countPaired(afterDelete->surface) : 0,
                        afterDelete ? afterDelete->collision.polygons.size() : 0,
                        countPaired(read.surface), read.collision.polygons.size()));
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

/// Adds `shape` to `room` drawn as its first face, at its first vertex, and
/// reads the scene back.
[[nodiscard]] std::expected<void, std::string> addShape(const Scene& scene, const ReadSurface& read,
                                                        const toki::scene::RoomMesh& room,
                                                        const toki::scene::MeshShape shape) {
    SceneModel model(scene);
    std::vector<toki::scene::RoomDraft> drafts{toki::scene::makeRoomDraft(room)};
    const glm::vec3 at = glm::round(room.vertices.front().position);
    const toki::scene::DraftFace like = drafts.front().faces.front();
    const std::vector<usize> added =
        toki::scene::addMeshShape(drafts.front(), shape, at, 100.0F, like);
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
    const std::optional<ReadSurface> after = readSurface(showModel(scene, model), &rebuild->kept);
    if (!after) {
        return std::unexpected("the collision is gone");
    }
    const toki::scene::RoomMesh* grown = toki::scene::findRoomMesh(after->mesh, room.room);
    if (grown == nullptr || grown->faces.size() != room.faces.size() + added.size()) {
        return std::unexpected(std::format("{} faces read back where {} were drafted",
                                           grown != nullptr ? grown->faces.size() : 0,
                                           room.faces.size() + added.size()));
    }
    const usize polygons = read.collision.polygons.size() + (like.collision ? added.size() : 0);
    if (after->collision.polygons.size() != polygons) {
        return std::unexpected(std::format("{} polygons read back where {} were drafted",
                                           after->collision.polygons.size(), polygons));
    }
    return {};
}

/// A scene and the mesh its last rebuild grouped its entries into.
struct Rebuilt {
    Scene scene;
    SceneMesh kept;
};

[[nodiscard]] std::expected<Rebuilt, std::string>
rebuildRoom(const Scene& scene, const ReadSurface& read, toki::scene::RoomDraft draft) {
    SceneModel model(scene);
    const std::vector<toki::scene::RoomDraft> drafts{std::move(draft)};
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
    if (const std::expected<void, base::Diagnostic> set = toki::scene::setCollisionGeometry(
            model, scene.layers[0].index,
            toki::scene::makeDraftedCollision(read.collision, read.surface, drafts, nullptr));
        !set) {
        return std::unexpected(set.error().message);
    }
    return Rebuilt{.scene = showModel(scene, model), .kept = rebuild->kept};
}

/// The scene read as the editor reads it, its mesh grouped as the first of
/// `kept` that grouped its bytes.
[[nodiscard]] std::expected<ReadSurface, std::string>
readEdited(const Scene& scene, const std::span<const SceneMesh* const> kept) {
    std::optional<ReadSurface> read = readSurface(scene, kept);
    if (!read) {
        return std::unexpected("the collision is gone");
    }
    static_cast<void>(toki::scene::makeSurfaceEdit(read->mesh, scene.layers[0].rooms.size(),
                                                   read->surface, read->collision));
    static_cast<void>(toki::scene::listSceneMaterials(scene, read->mesh));
    return std::move(*read);
}

[[nodiscard]] std::expected<Rebuilt, std::string> addBox(const Scene& scene,
                                                         const ReadSurface& read, usize room) {
    const toki::scene::RoomMesh* mesh = toki::scene::findRoomMesh(read.mesh, room);
    if (mesh == nullptr || mesh->faces.empty()) {
        return std::unexpected(std::format("room {} has no faces", room));
    }
    toki::scene::RoomDraft draft = toki::scene::makeRoomDraft(*mesh);
    const toki::scene::DraftFace like = draft.faces.front();
    static_cast<void>(toki::scene::addMeshShape(draft, toki::scene::MeshShape::Box,
                                                glm::round(mesh->vertices.front().position), 100.0F,
                                                like));
    return rebuildRoom(scene, read, std::move(draft));
}

/// Extrudes a solid face, adds a box, takes the box back as an undo does,
/// and adds a box again, reading the scene between as the editor does. Read
/// after the undo, grouped as the mesh with the box was, the room must read
/// as it did before the box: a `Vtx` array the box's rebuild reused by name
/// holds other vertices once undone, and the mesh read before the box groups
/// them.
[[nodiscard]] std::expected<void, std::string> editAndUndo(const Scene& scene) {
    const std::optional<ReadSurface> first = readSurface(scene);
    if (!first) {
        return std::unexpected("no collision");
    }
    const toki::scene::RoomMesh* room = nullptr;
    usize solid = 0;
    for (const toki::scene::RoomMesh& mesh : first->mesh.rooms) {
        const auto found = std::ranges::find_if(mesh.faces, [](const toki::scene::MeshFace& face) {
            return face.collision.has_value();
        });
        if (found != mesh.faces.end()) {
            room = &mesh;
            solid = static_cast<usize>(found - mesh.faces.begin());
            break;
        }
    }
    for (const toki::scene::RoomMesh& mesh : first->mesh.rooms) {
        if (room == nullptr && !mesh.faces.empty()) {
            room = &mesh;
        }
    }
    if (room == nullptr) {
        return std::unexpected("no room has faces");
    }
    toki::scene::RoomDraft extruded = toki::scene::makeRoomDraft(*room);
    const std::array<usize, 1> faces{solid};
    static_cast<void>(toki::scene::extrudeFaces(extruded, faces));
    const std::expected<Rebuilt, std::string> afterExtrude =
        rebuildRoom(scene, *first, std::move(extruded));
    if (!afterExtrude) {
        return std::unexpected("extrude: " + afterExtrude.error());
    }
    const std::expected<ReadSurface, std::string> second =
        readEdited(afterExtrude->scene, std::array{&afterExtrude->kept});
    if (!second) {
        return std::unexpected(second.error());
    }
    const std::expected<Rebuilt, std::string> afterBox =
        addBox(afterExtrude->scene, *second, room->room);
    if (!afterBox) {
        return std::unexpected("first box: " + afterBox.error());
    }
    const std::expected<ReadSurface, std::string> third =
        readEdited(afterBox->scene, std::array{&afterBox->kept});
    if (!third) {
        return std::unexpected(third.error());
    }
    for (const toki::scene::RoomMesh& kept : afterBox->kept.rooms) {
        const toki::scene::RoomMesh* read = toki::scene::findRoomMesh(third->mesh, kept.room);
        for (const toki::scene::MeshEntry& entry : kept.entries) {
            const auto same =
                read != nullptr
                    ? std::ranges::find(read->entries, entry.key, &toki::scene::MeshEntry::key)
                    : std::vector<toki::scene::MeshEntry>::const_iterator{};
            if (read == nullptr || same == read->entries.end() || same->vtx != entry.vtx) {
                return std::unexpected(std::format("the box's rebuild keeps {}[{}] as other bytes "
                                                   "than it writes",
                                                   entry.key.symbol, entry.key.index));
            }
        }
    }
    const std::expected<ReadSurface, std::string> undone =
        readEdited(afterExtrude->scene, std::array{&third->mesh, &second->mesh, &first->mesh});
    if (!undone) {
        return std::unexpected(undone.error());
    }
    const toki::scene::RoomMesh* before = toki::scene::findRoomMesh(second->mesh, room->room);
    const toki::scene::RoomMesh* after = toki::scene::findRoomMesh(undone->mesh, room->room);
    if (before == nullptr || after == nullptr ||
        before->vertices.size() != after->vertices.size()) {
        return std::unexpected(
            std::format("undone, the room reads {} vertices where it read {} before the box",
                        after != nullptr ? after->vertices.size() : 0,
                        before != nullptr ? before->vertices.size() : 0));
    }
    const std::expected<Rebuilt, std::string> again =
        addBox(afterExtrude->scene, *undone, room->room);
    if (!again) {
        return std::unexpected("box after undo: " + again.error());
    }
    return {};
}

} // namespace

TEST_CASE("a scene extruded, given a box, undone and given a box again reads back each time",
          "[scene][surface][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize edited = 0;
    for (const hackeroot::AssetDirectory& asset : workspace->getAssets().scenes) {
        if (!asset.isExtracted && asset.name != "example") {
            continue;
        }
        if (asset.name != "example" && edited >= 20) {
            continue;
        }
        hackeroot::AssetImageReader reader(workspace);
        const Scene scene = toki::scene::readScene(reader, asset);
        if (scene.layers.empty()) {
            continue;
        }
        INFO(asset.name);
        if (const std::expected<void, std::string> made = editAndUndo(scene); made) {
            ++edited;
        } else {
            failures.push_back(asset.name + ": " + made.error());
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n";
    }
    WARN(edited << " scenes edited, undone and edited again");
    INFO(listed);
    CHECK(edited > 0);
    CHECK(failures.empty());
}

TEST_CASE("a box and a plane added to every vanilla room read back with their polygons",
          "[scene][surface][checkout]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> failures;
    usize added = 0;
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
        if (!read) {
            continue;
        }
        for (const toki::scene::RoomMesh& room : read->mesh.rooms) {
            if (room.faces.empty()) {
                continue;
            }
            for (const toki::scene::MeshShape shape :
                 {toki::scene::MeshShape::Box, toki::scene::MeshShape::Plane}) {
                INFO(asset.name << " room " << room.room);
                if (const std::expected<void, std::string> made =
                        addShape(scene, *read, room, shape);
                    made) {
                    ++added;
                } else {
                    failures.push_back(
                        std::format("{} room {}: {}", asset.name, room.room, made.error()));
                }
            }
        }
    }
    std::string listed;
    for (const std::string& failure : failures) {
        listed += failure + "\n";
    }
    WARN(added << " shapes added and read back, " << failures.size() << " not");
    INFO(listed);
    CHECK(failures.empty());
}

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
    WARN(saved << " scenes with their surface edited read back, " << failures.size() << " did not");
    INFO(listed);
    CHECK(saved > 0);
    CHECK(failures.empty());
}
