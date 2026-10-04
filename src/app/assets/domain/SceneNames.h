#pragma once

#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"

#include <string>
#include <string_view>

namespace toki::assets {

/// A scene's name as a person would write it, from its scene table enum:
/// `SCENE_HYRULE_FIELD` is "Hyrule Field". Words keep their order; a name
/// without the `SCENE_` prefix is used whole.
[[nodiscard]] std::string makeReadableSceneName(std::string_view enumName);

/// The scene table entry for a scene folder: `spot00` is `spot00_scene`.
/// Nothing for a folder the table does not list, such as a fast64 export that
/// has not been registered yet.
[[nodiscard]] const hackeroot::SceneEntry* findSceneEntry(const hackeroot::Tables& tables,
                                                          std::string_view folderName);

/// The folder an asset sits in under `scenes/` or `objects/`, such as
/// `overworld` for `scenes/overworld/spot00`; "other" for one directly inside.
[[nodiscard]] std::string getAssetCategory(const hackeroot::AssetDirectory& asset);

} // namespace toki::assets
