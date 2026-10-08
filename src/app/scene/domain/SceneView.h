#pragma once

#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/ViewportFrame.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

/// How a scene is shown: which of its layers, under which light setting, which
/// rooms, whether placed actors are shown, and whether those with a model are
/// drawn as it rather than marked.
struct SceneView {
    /// Index into `Scene::layers`.
    usize layer = 0;
    usize lightSetting = 0;
    std::vector<bool> hiddenRooms;
    bool showsActors = true;
    bool drawsModels = true;
    /// The frame placed actors' animations are on while they play; nothing
    /// while they stand on the first.
    std::optional<usize> actorFrame;
    /// The player entry Link is drawn at; the others are marked.
    usize shownEntrance = 0;
};

/// How a scene is first shown: its main layer, at midday where the time of
/// day picks the light setting, every room, actors marked.
[[nodiscard]] SceneView makeSceneView(const Scene& scene);

/// `view` carried over to a scene read again, kept within what it has.
[[nodiscard]] SceneView keepSceneView(const Scene& scene, SceneView view);

/// How `scene` is shown when it opens as `session` last left it: the same
/// layer of the game, light setting and rooms, kept within what it has now.
[[nodiscard]] SceneView restoreSceneView(const Scene& scene, const core::SceneSession& session);

/// What `view` of `scene` is to open it again the same way. The camera is not
/// the view's to know, and is left out.
[[nodiscard]] core::SceneSession makeSceneSession(const Scene& scene, const SceneView& view);

/// What the viewport draws of a scene shown as `view`: for the opaque pass and
/// then the translucent one, what `Room_Draw` sets up, the default draw
/// config's `sDefaultDisplayList` and `sSetupDL[SETUPDL_25]`, then that pass's
/// display list of every part of every shown room, then that pass's placed
/// actors drawn as their models (`makeActorDraws`). Actors without one, and
/// player entries, are marked between the two passes.
[[nodiscard]] viewport::ViewportContent makeSceneContent(const Scene& scene, const SceneView& view);

/// The light and fog the game draws a scene under while `setting` is its
/// current light setting.
[[nodiscard]] viewport::ViewportLighting makeLighting(const LightSetting& setting);

/// What a light setting is called: its time of day where the time of day
/// picks the setting, its number otherwise.
[[nodiscard]] std::string makeLightSettingName(const SceneHeader& header, usize index);

} // namespace toki::scene
