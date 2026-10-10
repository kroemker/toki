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
    actor::ActorDatabase database =
        actor::readActorDatabase(actor::makeCheckoutDatabasePath(reader.getWorkspace().getRoot()));
    SceneActors actors = readSceneActors(reader, request.scene, request.layers, database);
    std::ranges::move(database.diagnostics, std::back_inserter(actors.diagnostics));
    std::ranges::move(database.problems, std::back_inserter(actors.diagnostics));
    return actors;
}

} // namespace

SceneActorLoader::SceneActorLoader()
    : WatchedReader(
          readRequested,
          [](const hackeroot::Workspace& workspace, const ActorRequest&) {
              return std::vector<std::filesystem::path>{
                  actor::makeCheckoutDatabasePath(workspace.getRoot())};
          },
          [](const SceneActors& actors) { return actors.watched; }) {}

} // namespace toki::scene
