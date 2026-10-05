#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/ViewportFrame.h"

#include <string>
#include <vector>

namespace toki::scene {

/// How a scene is shown: which of its layers, under which light setting, which
/// rooms, and whether placed actors are marked.
struct SceneView {
    /// Index into `Scene::layers`.
    usize layer = 0;
    usize lightSetting = 0;
    std::vector<bool> hiddenRooms;
    bool showsActors = true;
};

/// How a scene is first shown: its main layer, at midday where the time of
/// day picks the light setting, every room, actors marked.
[[nodiscard]] SceneView makeSceneView(const Scene& scene);

/// `view` carried over to a scene read again, kept within what it has.
[[nodiscard]] SceneView keepSceneView(const Scene& scene, SceneView view);

/// What the viewport draws of a scene shown as `view`: for the opaque pass and
/// then the translucent one, what `Room_Draw` sets up, the default draw
/// config's `sDefaultDisplayList` and `sSetupDL[SETUPDL_25]`, then that pass's
/// display list of every part of every shown room. Actor markers are drawn
/// between the two passes.
[[nodiscard]] viewport::ViewportContent makeSceneContent(const Scene& scene, const SceneView& view);

/// The light and fog the game draws a scene under while `setting` is its
/// current light setting.
[[nodiscard]] viewport::ViewportLighting makeLighting(const LightSetting& setting);

/// What a light setting is called: its time of day where the time of day
/// picks the setting, its number otherwise.
[[nodiscard]] std::string makeLightSettingName(const SceneHeader& header, usize index);

/// The index of `SETUPDL_25` among the setup display lists, which rooms are
/// drawn with.
inline constexpr usize RoomSetupDisplayList = 25;

} // namespace toki::scene
