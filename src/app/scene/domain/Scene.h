#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "z64/MemoryImage.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace toki::scene {

/// A scene read from its folder: what its header and its rooms' headers say,
/// and what the viewport draws of it, every room at once.
struct Scene {
    std::string name;
    std::filesystem::path directory;
    SceneHeader header;
    std::vector<RoomHeader> rooms;
    viewport::ViewportContent content;
    /// Every file the scene was read from.
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
    std::chrono::milliseconds elapsed{};
};

/// Reads a scene and all its rooms into memory images, as the game loads them,
/// and their headers out of those images. The game code the scene is drawn
/// with is read into the first image, the scene into the second, and each room
/// into one of its own.
[[nodiscard]] Scene readScene(hackeroot::AssetImageReader& reader,
                              const hackeroot::AssetDirectory& asset);

/// What the viewport draws of a scene, from the images `readScene` reads: for
/// the opaque pass and then the translucent one, what `Room_Draw` sets up, the
/// default draw config's `sDefaultDisplayList` and `sSetupDL[SETUPDL_25]`, then
/// that pass's display list of every part of every room.
[[nodiscard]] viewport::ViewportContent makeSceneContent(std::vector<z64::MemoryImage> images,
                                                         const std::vector<RoomHeader>& rooms);

/// The light setting a scene is shown under: midday where the time of day
/// picks the setting, the first one otherwise. Temporary, until the light
/// setting can be chosen.
[[nodiscard]] usize findShownLightSetting(const SceneHeader& header);

/// The light and fog the game draws a scene under while `setting` is its
/// current light setting.
[[nodiscard]] viewport::ViewportLighting makeLighting(const LightSetting& setting);

/// The index of `SETUPDL_25` among the setup display lists, which rooms are
/// drawn with.
inline constexpr usize RoomSetupDisplayList = 25;

} // namespace toki::scene
