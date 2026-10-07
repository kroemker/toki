#include "SceneActorLoader.h"

#include "app/assets/domain/WatchedReader.h"
#include "app/scene/domain/SceneActors.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <vector>

namespace toki::scene {

SceneActorLoader::SceneActorLoader()
    : WatchedReader(
          [](hackeroot::AssetImageReader& reader, const ActorRequest& request) {
              return readSceneActors(reader, request.layers);
          },
          [](const hackeroot::Workspace& /*workspace*/, const ActorRequest& /*request*/) {
              return std::vector<std::filesystem::path>{};
          },
          [](const SceneActors& actors) { return actors.watched; }) {}

} // namespace toki::scene
