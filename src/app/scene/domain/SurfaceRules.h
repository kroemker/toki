#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"

#include <map>
#include <optional>
#include <string>

namespace toki::scene {

struct CollisionNames;

/// A setting of a surface type that can have no effect, as the others are set
/// or as the polygons that have it lie.
enum class SurfaceField : u8 {
    Exit,
    FloorType,
    FloorProperty,
    FloorEffect,
    ConveyorSpeed,
    ConveyorDirection,
    Unknown18,
    ContactDamage,
};

/// Why settings have no effect, by setting.
using IgnoredSettings = std::map<SurfaceField, std::string>;

/// Why each setting of surface type `type` of `collision` that is set has no
/// effect, as the game's code reads them: "Ignored. Requires an exit." A
/// setting with an effect, or at zero, is not listed, except bits 18 to 20,
/// which no code reads. Values are named by `names` where it has them.
[[nodiscard]] IgnoredSettings listIgnoredSettings(const Collision& collision, usize type,
                                                  const CollisionNames* names);

/// Why `flag` of `polygon` of `collision` has no effect while set, if it has
/// none.
[[nodiscard]] std::optional<std::string>
findIgnoredFlag(const Collision& collision, const CollisionPolygon& polygon, PolygonFlag flag);

} // namespace toki::scene
