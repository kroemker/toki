#pragma once

#include "app/assets/Types.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::assets {

enum class AssetColumn : u8 {
    Name,
    Folder,
    Id,
    Rooms,
    Source,
};

/// One scene as the asset browser lists it: its folder, joined with what the
/// scene table says about it.
struct SceneRow {
    const hackeroot::AssetDirectory* asset = nullptr;
    /// The readable name when the scene table knows the scene, else the folder.
    std::string name;
    std::string category;
    /// Nothing for a folder the scene table does not list.
    const hackeroot::SceneEntry* entry = nullptr;
    /// How many entrances lead into the scene.
    usize entrances = 0;
};

struct ObjectRow {
    const hackeroot::AssetDirectory* asset = nullptr;
    /// The readable name of a vanilla object, else the folder.
    std::string name;
    /// Nothing for a folder the object table does not list.
    const hackeroot::ObjectEntry* entry = nullptr;
};

/// The workspace's scenes that match `filter` by name, folder or enum,
/// ignoring case.
[[nodiscard]] std::vector<SceneRow> makeSceneRows(const hackeroot::Workspace& workspace,
                                                  std::string_view filter);

[[nodiscard]] std::vector<ObjectRow> makeObjectRows(const hackeroot::Workspace& workspace,
                                                    std::string_view filter);

/// `rows` grouped by category, categories in alphabetical order, and sorted by
/// `column` within each. Ties keep name order.
[[nodiscard]] std::vector<SceneRow> sortSceneRows(std::vector<SceneRow> rows, AssetColumn column,
                                                  bool ascending);

/// `rows` sorted by `column`; `Rooms` sorts by name, since objects have none.
[[nodiscard]] std::vector<ObjectRow> sortObjectRows(std::vector<ObjectRow> rows, AssetColumn column,
                                                    bool ascending);

} // namespace toki::assets
