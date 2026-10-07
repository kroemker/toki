#pragma once

#include "app/assets/domain/WatchedReader.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"

namespace toki::scene {

/// Reads the open scene, and reads it again whenever one of its files changes.
class SceneLoader : public assets::WatchedAssetReader<Scene> {
public:
    SceneLoader();
};

} // namespace toki::scene
