#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "z64/MemoryImage.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace toki::scene {

/// What a collision's polygons are coloured by: which way each faces, its
/// surface type, or one of its surface type's settings, where those without
/// the setting are grey.
enum class CollisionColoring : u8 {
    Kind,
    SurfaceType,
    Material,
    FloorType,
    WallType,
    Exit,
    Hookshot,
    Conveyor,
};

inline constexpr usize CollisionColoringCount = static_cast<usize>(CollisionColoring::Conveyor) + 1;

/// What `coloring` is called where it is chosen.
[[nodiscard]] std::string_view getCollisionColoringLabel(CollisionColoring coloring);

/// How a collision is drawn.
struct CollisionLook {
    CollisionColoring coloring = CollisionColoring::Kind;
    /// Drawn over the rooms, see-through, rather than in their place.
    bool isTranslucent = false;
    /// The surface type whose polygons are tinted, as the selected polygon's.
    std::optional<u16> tintedSurfaceType;
};

/// The segment the collision's image is written to, one no asset or game code
/// of OoT uses.
inline constexpr u8 CollisionSegment = 0x0F;

/// A collision as a memory image of display lists built by hand: one that
/// sets up how its polygons are drawn, then one per polygon, so each is a draw
/// of its own that can be picked, and the same for its waterboxes.
struct CollisionDrawing {
    z64::MemoryImage image;
    u32 setup = 0;
    /// Zero for a polygon that names a vertex the collision does not hold.
    std::vector<u32> polygons;
    u32 waterBoxSetup = 0;
    std::vector<u32> waterBoxes;
};

/// Draws each polygon of `collision` flat in the colour `look` picks for it,
/// shaded by which way it faces, depth tested and not culled. Translucent, it
/// is drawn as a decal, so that it shows on the room surface it lies on or
/// just behind. Each waterbox is a see-through blue box from its surface down
/// to the collision's lowest point, drawn over whatever is drawn before it.
[[nodiscard]] CollisionDrawing makeCollisionDrawing(const Collision& collision,
                                                    const CollisionLook& look);

/// The colour polygons of surface type `type` are coloured by Surface Type.
[[nodiscard]] std::array<u8, 3> findSurfaceTypeColor(usize type);

/// The colour `look` gives `polygon` before it is shaded.
[[nodiscard]] std::array<u8, 3> findPolygonColor(const Collision& collision,
                                                 const CollisionPolygon& polygon,
                                                 const CollisionLook& look);

} // namespace toki::scene
