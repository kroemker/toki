#pragma once

#include "app/scene/Types.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"

#include <array>
#include <vector>

namespace toki::scene {

/// A `CollisionPoly`, its words as the game reads them. The top three bits of
/// the first two vertex words are flags, the rest the vertex's index.
struct CollisionPolygon {
    /// Which of the collision's surface types the polygon has.
    u16 type = 0;
    std::array<u16, 3> vertexData{};
    /// The unit normal, 0x7FFF being 1.
    std::array<s16, 3> normal{};
    /// The plane's distance from the origin along the normal.
    s16 distance = 0;

    friend bool operator==(const CollisionPolygon&, const CollisionPolygon&) = default;
};

/// Which way a polygon faces, as the game sorts it into its floor, wall and
/// ceiling lists by its normal's height.
enum class PolygonKind : u8 {
    Floor,
    Wall,
    Ceiling,
};

/// A `SurfaceType`: the two words of settings polygons share.
struct SurfaceType {
    std::array<u32, 2> data{};

    friend bool operator==(const SurfaceType&, const SurfaceType&) = default;
};

/// A surface type's settings, each as the `SurfaceType_Get*` function of the
/// same name in `z_bgcheck.c` takes it out of the words.
struct SurfaceFields {
    u8 bgCamIndex = 0;
    /// One past the index into the scene's exit list, zero for none.
    u8 exitIndex = 0;
    u8 floorType = 0;
    u8 unk18 = 0;
    u8 wallType = 0;
    u8 floorProperty = 0;
    bool isSoft = false;
    bool isHorseBlocked = false;
    u8 material = 0;
    u8 floorEffect = 0;
    u8 lightSetting = 0;
    u8 echo = 0;
    bool canHookshot = false;
    u8 conveyorSpeed = 0;
    /// In 64ths of a turn.
    u8 conveyorDirection = 0;
    bool unk27 = false;

    friend bool operator==(const SurfaceFields&, const SurfaceFields&) = default;
};

/// A `BgCamInfo`: the camera setting surfaces and waterboxes name by index,
/// and the segmented address of its data, zero for none.
struct CollisionCamera {
    u16 setting = 0;
    s16 count = 0;
    u32 data = 0;

    friend bool operator==(const CollisionCamera&, const CollisionCamera&) = default;
};

/// A `WaterBox`: water from its surface down, over a rectangle on the ground.
struct WaterBox {
    s16 xMin = 0;
    s16 ySurface = 0;
    s16 zMin = 0;
    s16 xLength = 0;
    s16 zLength = 0;
    u32 properties = 0;

    friend bool operator==(const WaterBox&, const WaterBox&) = default;
};

/// The room a waterbox belongs to that says it belongs to every room.
inline constexpr u8 WaterBoxAllRooms = 0x3F;

/// A `CollisionHeader` and the lists it points at, read out of the scene's
/// bytes. The surface type and camera lists hold no count; each is as long as
/// the array laid out at its address.
struct Collision {
    /// Where the header is, as a segmented address.
    u32 address = 0;
    std::array<s16, 3> minBounds{};
    std::array<s16, 3> maxBounds{};
    std::vector<std::array<s16, 3>> vertices;
    std::vector<CollisionPolygon> polygons;
    std::vector<SurfaceType> surfaceTypes;
    std::vector<CollisionCamera> cameras;
    std::vector<WaterBox> waterBoxes;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the collision header at `address` in `image`. A polygon naming a
/// vertex or a surface type the lists do not hold, or whose normal is not its
/// vertices', is a diagnostic naming it; the polygon is kept as it is.
[[nodiscard]] Collision readCollision(const z64::MemoryImage& image, u32 address);

/// The index of a polygon's `corner`th vertex, without its flags.
[[nodiscard]] u16 getVertexIndex(const CollisionPolygon& polygon, usize corner);

/// The flags in the top three bits of a polygon's `corner`th vertex word.
[[nodiscard]] u8 getVertexFlags(const CollisionPolygon& polygon, usize corner);

[[nodiscard]] PolygonKind findPolygonKind(const CollisionPolygon& polygon);

[[nodiscard]] SurfaceFields readSurfaceFields(const SurfaceType& surface);

/// How many polygons have each of the collision's surface types, by index.
[[nodiscard]] std::vector<usize> countSurfacePolygons(const Collision& collision);

[[nodiscard]] u8 getWaterBoxCamera(const WaterBox& box);

[[nodiscard]] u8 getWaterBoxLightSetting(const WaterBox& box);

[[nodiscard]] u8 getWaterBoxRoom(const WaterBox& box);

} // namespace toki::scene
