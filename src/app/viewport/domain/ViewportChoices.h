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

/// What the owner of the viewport's content lets its bar choose, as a scene
/// offers its light settings and how its actors are shown. A choice the
/// content does not have is left empty and has no row.
struct ViewportChoices {
    std::vector<std::string> lightSettings;
    /// Index into `lightSettings`.
    usize lightSetting = 0;
    std::optional<ActorDisplay> actors;
    /// Whether the content has collision, and whether it is drawn, which the
    /// game never does; the bar's Overlays show and hide it.
    bool hasCollision = false;
    bool showsCollision = false;
    /// Whether the content's waterboxes are drawn, shown or hidden by the
    /// content's owner, not the bar.
    bool showsWaterBoxes = false;
    /// What shown collision can be coloured by, and which it is, an index into
    /// them.
    std::vector<std::string> collisionColorings;
    usize collisionColoring = 0;
    /// Whether the content has faces, edges and vertices to edit; when not,
    /// only node mode is offered.
    bool canEdit = false;

    friend bool operator==(const ViewportChoices&, const ViewportChoices&) = default;
};

} // namespace toki::viewport
