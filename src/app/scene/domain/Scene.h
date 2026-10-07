#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "z64/MemoryImage.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace toki::scene {

/// One layer of a scene: what its header and every room's header say for one
/// value of the game's scene layer, which picks Link's age, the time of day
/// or a cutscene. A room with no header of its own for the layer has its main
/// header here, as the game falls back to it.
struct SceneLayer {
    usize index = 0;
    SceneHeader header;
    std::vector<RoomHeader> rooms;
};

/// A scene read from its folder: the images the game loads for it and what
/// their headers say, layer by layer.
struct Scene {
    std::string name;
    std::filesystem::path directory;
    /// The game code the scene is drawn with, the scene, then each room.
    /// Shared with what the viewport draws of the scene, which is made again
    /// whenever the layer or the light changes; an image never changes once
    /// read.
    std::vector<std::shared_ptr<const z64::MemoryImage>> images;
    /// The main layer first, then every alternate layer the scene has.
    std::vector<SceneLayer> layers;
    /// The models of the actors the layers place, and the objects they are drawn from.
    SceneActors actors;
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
    std::chrono::milliseconds elapsed{};
};

/// Where in `Scene::images` the scene is, and the first room.
inline constexpr usize SceneImage = 1;
inline constexpr usize FirstRoomImage = 2;

/// Reads a scene and all its rooms into memory images, as the game loads them,
/// and every layer's headers out of those images.
[[nodiscard]] Scene readScene(hackeroot::AssetImageReader& reader,
                              const hackeroot::AssetDirectory& asset);

/// What a scene layer is called: the four the game plays in, then its
/// cutscenes.
[[nodiscard]] std::string makeLayerName(usize index);

} // namespace toki::scene
