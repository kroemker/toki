#pragma once

#include "app/viewport/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::viewport {

/// How placed actors are shown: not at all, each marked, or each that has a
/// model drawn as it.
enum class ActorDisplay : u8 {
    Hidden,
    Markers,
    Models,
};

/// How a scene's collision is shown: not at all, over the rooms and
/// see-through, or alone in their place.
enum class CollisionDisplay : u8 {
    Off,
    OverRooms,
    Only,
};

/// What the owner of the viewport's content lets its bar choose, as a scene
/// offers its light settings and how its actors are shown. A choice the
/// content does not have is left empty and has no row.
struct ViewportChoices {
    std::vector<std::string> lightSettings;
    /// Index into `lightSettings`.
    usize lightSetting = 0;
    std::optional<ActorDisplay> actors;
    std::optional<CollisionDisplay> collision;
    /// What the collision can be coloured by, and which it is, an index into
    /// them.
    std::vector<std::string> collisionColorings;
    usize collisionColoring = 0;

    friend bool operator==(const ViewportChoices&, const ViewportChoices&) = default;
};

} // namespace toki::viewport
