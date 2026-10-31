#pragma once

#include "app/scene/Types.h"

#include <string>
#include <vector>

namespace toki::scene {

class SceneModel;
struct Scene;

enum class GeometryChangeKind : u8 {
    VerticesMoved,
    Repacked,
    Rebuilt,
    Added,
    Removed,
    BoundsUpdated,
    CollisionUpdated,
};

/// What a save does to one symbol of the rooms' geometry or the collision:
/// how it changed, its commands before and after for a display list, and how
/// many bytes it grew by.
struct GeometryChange {
    std::string symbol;
    GeometryChangeKind kind = GeometryChangeKind::Rebuilt;
    usize commandsBefore = 0;
    usize commandsAfter = 0;
    s64 byteDelta = 0;
};

/// What the save plan's table shows: the symbols of geometry `edited` holds
/// other than `read` did, and how many display lists and vertex arrays are
/// written as they were read.
struct GeometryChanges {
    std::vector<GeometryChange> changes;
    usize unchanged = 0;
};

[[nodiscard]] GeometryChanges listGeometryChanges(const Scene& read, const SceneModel& edited);

[[nodiscard]] const char* getGeometryChangeName(GeometryChangeKind kind);

} // namespace toki::scene
