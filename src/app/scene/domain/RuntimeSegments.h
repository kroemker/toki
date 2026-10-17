#pragma once

#include "app/scene/Types.h"
#include "z64/MemoryImage.h"

namespace toki::scene {

/// The segment the stand-in's image is written to, one no asset or game code
/// of OoT uses.
inline constexpr u8 StandInSegment = 0x0E;

/// The segments a scene's draw config points at what it builds as it runs:
/// display lists that scroll textures, and in Jabu-Jabu's belly a matrix.
inline constexpr u8 FirstRuntimeSegment = 0x08;
inline constexpr u8 LastRuntimeSegment = 0x0D;

/// One stand-in for every runtime segment: an identity matrix followed by
/// `gsSPEndDisplayList`. An identity `Mtx` reads as `G_NOOP`s, so called as a
/// display list it returns at once, as `gEmptyDL` does, and loaded as a matrix
/// it changes nothing, where `gEmptyDL` loaded as one throws every vertex far
/// off. A draw of it is where the segments are pointed at it.
///
/// A workaround while scene draw configs are not run: each points these
/// segments at what one frame of its animation shows instead.
struct RuntimeSegments {
    z64::MemoryImage image;
    u32 standIn = 0;
};

[[nodiscard]] RuntimeSegments makeRuntimeSegments();

} // namespace toki::scene
