#include "SceneActorLoader.h"

#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/assets/domain/WatchedReader.h"
#include "app/scene/domain/SceneActors.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] SceneActors readRequested(hackeroot::AssetImageReader& reader,
                                        const ActorRequest& request) {
    ActorDatabases databases{.shipped = actor::readActorDatabase(request.shippedDatabase),
                             .checkout = actor::readActorDatabase(
                                 actor::makeCheckoutDatabasePath(reader.getWorkspace().getRoot()))};
    SceneActors actors = readSceneActors(reader, request.scene, request.layers, databases);
    std::ranges::move(databases.shipped.diagnostics, std::back_inserter(actors.diagnostics));
    std::ranges::move(databases.checkout.diagnostics, std::back_inserter(actors.diagnostics));
    return actors;
}

} // namespace

SceneActorLoader::SceneActorLoader()
    : WatchedReader(
          readRequested,
          [](const hackeroot::Workspace& workspace, const ActorRequest& request) {
              return std::vector<std::filesystem::path>{
                  request.shippedDatabase, actor::makeCheckoutDatabasePath(workspace.getRoot())};
          },
          [](const SceneActors& actors) { return actors.watched; }) {}

} // namespace toki::scene
