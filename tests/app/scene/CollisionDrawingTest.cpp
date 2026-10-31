#include "app/scene/domain/CollisionDrawing.h"

#include "app/scene/domain/Collision.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

using base::u8;

namespace {

/// A floor and a wall of one surface type each, the wall's hookshotable and
/// an exit, and a polygon naming a vertex the collision does not hold.
toki::scene::Collision makeCollision() {
    toki::scene::Collision collision;
    collision.vertices = {{0, 0, 0}, {0, 0, 100}, {100, 0, 0}, {0, 100, 0}};
    collision.polygons = {{.type = 0, .vertexData = {0, 1, 2}, .normal = {0, 0x7FFF, 0}},
                          {.type = 1, .vertexData = {0, 3, 1}, .normal = {0x7FFF, 0, 0}},
                          {.type = 0, .vertexData = {0, 9, 1}, .normal = {0x7FFF, 0, 0}}};
    collision.surfaceTypes = {{.data = {0, 0}}, {.data = {2U << 8, 1U << 17}}};
    return collision;
}

} // namespace

TEST_CASE("a collision is a display list per polygon it can draw", "[scene][collision]") {
    const toki::scene::Collision collision = makeCollision();

    const toki::scene::CollisionDrawing drawing = toki::scene::makeCollisionDrawing(collision, {});

    REQUIRE(drawing.setup != 0);
    REQUIRE(drawing.polygons.size() == 3);
    REQUIRE(drawing.polygons[0] != 0);
    REQUIRE(drawing.polygons[1] != 0);
    REQUIRE(drawing.polygons[2] == 0);
    REQUIRE(drawing.image.segments.front().number == toki::scene::CollisionSegment);
}

TEST_CASE("a waterbox is a box down to the lowest point, or only its surface above it",
          "[scene][collision]") {
    toki::scene::Collision collision;
    collision.minBounds = {0, -200, 0};
    collision.waterBoxes = {{.ySurface = 0, .xLength = 100, .zLength = 100},
                            {.ySurface = -200, .xLength = 100, .zLength = 100}};

    const toki::scene::CollisionDrawing drawing = toki::scene::makeCollisionDrawing(collision, {});

    REQUIRE(drawing.waterBoxSetup != 0);
    REQUIRE(drawing.waterBoxes.size() == 2);
    REQUIRE(drawing.waterBoxes[0] != drawing.waterBoxes[1]);
}

TEST_CASE("polygons are coloured by kind, surface type or one setting", "[scene][collision]") {
    const toki::scene::Collision collision = makeCollision();
    const toki::scene::CollisionPolygon& floor = collision.polygons[0];
    const toki::scene::CollisionPolygon& wall = collision.polygons[1];
    const auto colorOf = [&collision](const toki::scene::CollisionPolygon& polygon,
                                      const toki::scene::CollisionLook& look) {
        return toki::scene::findPolygonColor(collision, polygon, look);
    };

    REQUIRE(colorOf(floor, {}) != colorOf(wall, {}));
    const toki::scene::CollisionLook bySurface{.coloring =
                                                   toki::scene::CollisionColoring::SurfaceType};
    REQUIRE(colorOf(floor, bySurface) != colorOf(wall, bySurface));
    REQUIRE(colorOf(floor, bySurface) == colorOf(collision.polygons[2], bySurface));

    for (const toki::scene::CollisionColoring coloring :
         {toki::scene::CollisionColoring::Hookshot, toki::scene::CollisionColoring::Exit}) {
        const toki::scene::CollisionLook look{.coloring = coloring};
        REQUIRE(colorOf(floor, look) != colorOf(wall, look));
        REQUIRE(colorOf(floor, look) ==
                colorOf(floor, {.coloring = toki::scene::CollisionColoring::Conveyor}));
    }

    const toki::scene::CollisionLook tinted{.tintedSurfaceType = 0};
    REQUIRE(colorOf(floor, tinted) != colorOf(floor, {}));
    REQUIRE(colorOf(wall, tinted) == colorOf(wall, {}));
}
