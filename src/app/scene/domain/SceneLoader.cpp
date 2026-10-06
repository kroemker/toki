#include "SceneLoader.h"

#include "app/assets/domain/WatchedAssetReader.h"
#include "app/scene/domain/Scene.h"
#include "hackeroot/Assets.h"
#include "hackeroot/SceneFiles.h"

namespace toki::scene {

SceneLoader::SceneLoader()
    : WatchedAssetReader(readScene, [](const hackeroot::Workspace& /*workspace*/,
                                       const hackeroot::AssetDirectory& asset) {
          return hackeroot::findSceneFiles(asset).getAllFiles();
      }) {}

} // namespace toki::scene
