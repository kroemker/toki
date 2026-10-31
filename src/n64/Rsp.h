#pragma once

#include "n64/Memory.h"
#include "n64/RspResult.h"
#include "n64/Segment.h"
#include "n64/Types.h"

#include <array>

namespace n64 {

/// Departures from F3DEX2 a viewer asks of a task, to see past what the game
/// would draw. None is something the hardware can do.
struct RspOverrides {
    /// Lit vertices are shaded white rather than by the lights, so their
    /// textures show unshaded. Texture generation still uses their normals.
    bool lightsWhite = false;
    /// No triangle is culled for the way it faces.
    bool ignoresCulling = false;
};

struct RspTask {
    /// Where the display list starts, as a segmented address.
    u32 displayList = 0;
    /// The segment table at the start of the task, as physical addresses.
    std::array<u32, SegmentCount> segments{};
    /// Whether to record every command run (`RspResult::trace`), for looking
    /// into what a frame did.
    bool recordsTrace = false;
    /// Whether every triangle records where its vertices came from
    /// (`Triangle::sources`), for reading a mesh back out of its display list.
    bool recordsSources = false;
    RspOverrides overrides;
};

/// Runs one graphics task the way F3DEX2 would: reads the display list out of
/// `memory`, follows its calls and branches, and produces what the RSP sends
/// the RDP. Every command it cannot run, and every read outside mapped memory,
/// is a diagnostic naming the command's address; an unreadable command ends
/// the task, anything else is skipped.
[[nodiscard]] RspResult runTask(const Memory& memory, const RspTask& task);

} // namespace n64
