#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "base/Diagnostic.h"

#include <expected>
#include <span>

namespace toki::scene {

class SceneModel;

/// A flag a polygon holds in the top bits of one of its vertex words: what
/// passes through it, in the first, and whether it is a conveyor floor, in the
/// second.
enum class PolygonFlag : u8 {
    IgnoreCamera,
    IgnoreEntity,
    IgnoreProjectiles,
    Conveyor,
};

/// The most waterboxes a collision header counts.
inline constexpr usize MaximumWaterBoxes = 0xFFFF;

/// Sets the surface type at `index`. This and every edit of collision after it
/// changes the collision header the scene's header for `layer`, the game's
/// scene layer, points at, and the lists it points at, where they are:
/// collision belongs to the scene, and every layer that reads the same header
/// sees the edit. `makeOwnCollision` gives one layer a copy first.
[[nodiscard]] std::expected<void, base::Diagnostic>
setSurfaceType(SceneModel& model, usize layer, usize index, const SurfaceType& surface);

/// Adds `surface` to the end of the surface type list and returns its index.
[[nodiscard]] std::expected<usize, base::Diagnostic> addSurfaceType(SceneModel& model, usize layer,
                                                                    const SurfaceType& surface);

/// Removes every surface type no polygon has, the polygons' indices moved
/// down with them, and returns how many were removed. The last one is kept.
[[nodiscard]] std::expected<usize, base::Diagnostic> removeUnusedSurfaceTypes(SceneModel& model,
                                                                              usize layer);

/// Gives each of `polygons` the surface type at `type`.
[[nodiscard]] std::expected<void, base::Diagnostic>
setPolygonSurfaceType(SceneModel& model, usize layer, std::span<const usize> polygons, u16 type);

/// Sets or clears `flag` on each of `polygons`.
[[nodiscard]] std::expected<void, base::Diagnostic> setPolygonFlag(SceneModel& model, usize layer,
                                                                   std::span<const usize> polygons,
                                                                   PolygonFlag flag, bool isSet);

/// Whether `polygon` holds `flag`.
[[nodiscard]] bool hasPolygonFlag(const CollisionPolygon& polygon, PolygonFlag flag);

/// `polygon` with `flag` set or cleared.
[[nodiscard]] CollisionPolygon makeFlaggedPolygon(CollisionPolygon polygon, PolygonFlag flag,
                                                  bool isSet);

/// The most vertices a polygon's vertex words can name.
inline constexpr usize MaximumCollisionVertices = 0x2000;

/// Writes the vertices and polygons of `collision` over the lists the
/// collision header of `layer` points at, with their counts and the header's
/// bounds.
[[nodiscard]] std::expected<void, base::Diagnostic>
setCollisionGeometry(SceneModel& model, usize layer, const Collision& collision);

/// Sets the waterbox at `index`.
[[nodiscard]] std::expected<void, base::Diagnostic> setWaterBox(SceneModel& model, usize layer,
                                                                usize index, const WaterBox& box);

/// Adds `box` to the end of the waterbox list, made for the header when it
/// has none, and returns its index.
[[nodiscard]] std::expected<usize, base::Diagnostic> addWaterBox(SceneModel& model, usize layer,
                                                                 const WaterBox& box);

/// Removes the waterbox at `index`. Removing the last one leaves the header
/// with no list, and removes the list once nothing points at it.
[[nodiscard]] std::expected<void, base::Diagnostic> removeWaterBox(SceneModel& model, usize layer,
                                                                   usize index);

/// Sets the camera setting of the camera at `index`.
[[nodiscard]] std::expected<void, base::Diagnostic> setCameraSetting(SceneModel& model, usize layer,
                                                                     usize index, u16 setting);

/// Gives the layer a collision header of its own, with its own copy of each
/// list the header points at, so that an edit of it shows in no other layer.
/// The scene's header for the layer is made its own first, as any edit of a
/// layer's header does. Nothing is copied when no other layer reads the
/// header.
[[nodiscard]] std::expected<void, base::Diagnostic> makeOwnCollision(SceneModel& model,
                                                                     usize layer);

} // namespace toki::scene
