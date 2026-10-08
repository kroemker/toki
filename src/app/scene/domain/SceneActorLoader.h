#pragma once

#include "app/assets/domain/WatchedReader.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"

#include <filesystem>
#include <string>
#include <vector>

namespace toki::scene {

/// What a scene's actors are read for: the layers that place them, and a name
/// to say what is being read.
struct ActorRequest {
    std::string name;
    /// The scene's folder, `ydan`.
    std::string scene;
    std::vector<SceneLayer> layers;
    /// The shipped actor database; the checkout's is found in the checkout.
    std::filesystem::path shippedDatabase;
};

/// Reads the open scene's actors after the scene itself, on a worker of its
/// own, so the scene shows before its actors' objects are read, by the shipped
/// actor database and the checkout's, read afresh each time. Reads them again
/// whenever either database, or an overlay or object they were read from,
/// changes.
class SceneActorLoader : public assets::WatchedReader<ActorRequest, SceneActors> {
public:
    SceneActorLoader();
};

} // namespace toki::scene
