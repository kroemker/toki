#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace toki::scene {

/// `polygon` with the unit normal and distance its corners in `vertices`
/// make, or nothing when they are in one line.
[[nodiscard]] std::optional<CollisionPolygon>
makeFacedPolygon(CollisionPolygon polygon, const std::vector<std::array<s16, 3>>& vertices);

/// `polygon` made of `material`: its surface type and its flags, the bits of
/// its vertex words no flag holds kept.
[[nodiscard]] CollisionPolygon makeMaterialPolygon(CollisionPolygon polygon,
                                                   const CollisionMaterial& material);

/// `collision` with its bounds around every vertex.
[[nodiscard]] Collision makeBoundedCollision(Collision collision);

/// A polygon as an edit of the surface leaves it: where its corners stand,
/// what it is made of, and the polygon it was read as, if any.
struct SurfacePolygon {
    std::array<std::array<s16, 3>, 3> corners{};
    CollisionMaterial material;
    std::optional<usize> read;
};

/// The polygons of `collision` as they were read.
[[nodiscard]] std::vector<SurfacePolygon> listSurfacePolygons(const Collision& collision);

/// Whether `corners` are in a line, so that no polygon can be faced at them.
[[nodiscard]] bool isInLine(const std::array<std::array<s16, 3>, 3>& corners);

/// Whether `makeSurfaceCollision` leaves `polygon` out of `collision`: its
/// corners are in a line, and it is not one read standing where it stood.
[[nodiscard]] bool isPolygonLeftOut(const Collision& collision, const SurfacePolygon& polygon);

/// `collision` with `polygons` in place of its own, in their order. A polygon
/// read whose corners stand where they stood is kept as it was, its normal
/// and distance with it, and only made of its material; every other is
/// faced again, its corners at vertices standing where they stand, or new
/// ones after them. One whose corners are in a line is left out. A vertex no
/// polygon has any more is left out, and the bounds are made again when the
/// vertices changed.
[[nodiscard]] Collision makeSurfaceCollision(const Collision& collision,
                                             std::span<const SurfacePolygon> polygons);

} // namespace toki::scene
