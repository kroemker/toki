#pragma once

#include "app/scene/Types.h"
#include "z64/MemoryImage.h"

#include <array>
#include <span>

namespace toki::scene {

enum class MarkerKind : u8 {
    Actor,
    PlayerEntry,
    TransitionActor,
};

/// Where something is placed in a scene, to be marked in the viewport.
struct Marker {
    std::array<s16, 3> position{};
    MarkerKind kind = MarkerKind::Actor;
};

/// The segment marker images are written to, one no asset or game code of
/// OoT uses.
inline constexpr u8 MarkerSegment = 0x0F;

struct ActorMarkers {
    z64::MemoryImage image;
    u32 displayList = 0;
};

/// A small diamond at every marker, coloured by kind and outlined in near black,
/// as a memory image of one display list built by hand. The outline is a
/// slightly larger diamond of which only the far side is drawn, so it shows as
/// a rim around the near one. Depth tested, so a marker inside a wall is hidden
/// as the actor would be.
[[nodiscard]] ActorMarkers makeActorMarkers(std::span<const Marker> markers);

} // namespace toki::scene
