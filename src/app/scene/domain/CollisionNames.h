#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "base/Diagnostic.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

/// A setting of a surface type whose values Toki names.
enum class SurfaceSetting : u8 {
    FloorType,
    FloorProperty,
    WallType,
    Material,
    FloorEffect,
    ConveyorSpeed,
};

/// A value of a surface type's setting: the constant the decomp gives it,
/// what Toki calls it, and what the game does with it.
struct SurfaceValueName {
    s64 value = 0;
    std::string constant;
    std::string name;
    std::string tooltip;

    friend bool operator==(const SurfaceValueName&, const SurfaceValueName&) = default;
};

/// What Toki calls the values of the surface type settings, from its own
/// `collision.toml`. A value it has no name for is shown as its number.
struct CollisionNames {
    std::map<SurfaceSetting, std::vector<SurfaceValueName>> values;
    std::vector<base::Diagnostic> diagnostics;

    /// The name of `value` of `setting`, if Toki has one.
    [[nodiscard]] const SurfaceValueName* findName(SurfaceSetting setting, s64 value) const;
};

/// What `setting` of `fields` is called: its name, or its number where
/// `names` has none.
[[nodiscard]] std::string makeSurfaceValueLabel(const CollisionNames& names, SurfaceSetting setting,
                                                const SurfaceFields& fields);

/// A surface type in a few words: its material, then each other named
/// setting that is not zero, and what it is.
[[nodiscard]] std::string makeSurfaceSummary(const CollisionNames& names,
                                             const SurfaceFields& fields);

/// Where Toki's collision names are installed: `collision.toml` in the assets
/// folder beside the executable.
[[nodiscard]] std::filesystem::path
makeShippedCollisionNamesPath(const std::filesystem::path& assetDirectory);

/// Reads the text of `collision.toml`. An entry that cannot be read is a
/// diagnostic naming the file, the table and the entry, and is left out.
[[nodiscard]] CollisionNames parseCollisionNames(std::string_view text,
                                                 const std::filesystem::path& file);

/// Reads `file`; one that cannot be read is a diagnostic and no names.
[[nodiscard]] CollisionNames readCollisionNames(const std::filesystem::path& file);

} // namespace toki::scene
