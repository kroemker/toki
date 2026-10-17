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

    friend bool operator==(const ViewportChoices&, const ViewportChoices&) = default;
};

} // namespace toki::viewport
