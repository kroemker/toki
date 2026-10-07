#pragma once

#include "app/assets/domain/WatchedReader.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"

#include <string>
#include <vector>

namespace toki::scene {

/// What a scene's actors are read for: the layers that place them, and a name
/// to say what is being read.
struct ActorRequest {
    std::string name;
    std::vector<SceneLayer> layers;
};

/// Reads the open scene's actors after the scene itself, on a worker of its
/// own, so the scene shows before its actors' objects are read, and reads them
/// again whenever an overlay or object they were read from changes. Its images
/// are kept apart from the scene loader's.
class SceneActorLoader : public assets::WatchedReader<ActorRequest, SceneActors> {
public:
    SceneActorLoader();
};

} // namespace toki::scene
