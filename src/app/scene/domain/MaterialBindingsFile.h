#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MaterialBindings.h"
#include "base/Diagnostic.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

/// The version of a scene's `.toki/scenes/<scene>.toml` this build reads and
/// writes.
inline constexpr s64 SceneFileFormat = 1;

/// Where a checkout keeps what Toki knows of the scene `scene` beyond its C:
/// `.toki/scenes/<scene>.toml` under it.
[[nodiscard]] std::filesystem::path makeSceneFilePath(const std::filesystem::path& root,
                                                      std::string_view scene);

/// The material bindings of a scene's file, and what of it could not be read.
struct ReadBindings {
    MaterialBindings bindings;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the bindings out of the text of a scene's file. Whatever cannot be
/// read is a diagnostic naming it, and is left out.
[[nodiscard]] ReadBindings parseMaterialBindings(std::string_view text,
                                                 const std::filesystem::path& file);

/// Reads a scene's file, nothing when there is none.
[[nodiscard]] ReadBindings readMaterialBindings(const std::filesystem::path& file);

/// The text of a scene's file for `bindings`, materials in a stable order.
[[nodiscard]] std::string formatMaterialBindings(const MaterialBindings& bindings);

} // namespace toki::scene
