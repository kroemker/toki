#include "SceneLoader.h"

#include "app/assets/domain/WatchedReader.h"
#include "app/scene/domain/Scene.h"
#include "hackeroot/Assets.h"
#include "hackeroot/SceneFiles.h"

#include <filesystem>
#include <vector>

namespace toki::scene {

SceneLoader::SceneLoader()
    : WatchedReader(readScene, [](const hackeroot::Workspace& /*workspace*/,
                                  const hackeroot::AssetDirectory& asset) {
          std::vector<std::filesystem::path> watched =
              hackeroot::findSceneFiles(asset).getAllFiles();
          watched.push_back(asset.directory);
          return watched;
      }) {}

} // namespace toki::scene
