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
    /// Whether the content's collision and waterboxes are drawn, which the
    /// game never draws. Shown or hidden by the content's owner, not the bar.
    bool showsCollision = false;
    bool showsWaterBoxes = false;
    /// What shown collision can be coloured by, and which it is, an index into
    /// them.
    std::vector<std::string> collisionColorings;
    usize collisionColoring = 0;
    /// What faces, edges and vertices are picked of, as a scene's mesh or its
    /// collision, and which, an index into them. Empty when the content has
    /// nothing to edit by them, and only node mode is offered.
    std::vector<std::string> editTargets;
    usize editTarget = 0;
    /// Whether what the edit target is linked to follows its edits, where
    /// the target has a link; `linkTooltip` says what the link does.
    std::optional<bool> isLinked;
    std::string linkTooltip;

    friend bool operator==(const ViewportChoices&, const ViewportChoices&) = default;
};

} // namespace toki::viewport
