#pragma once

#include "z64/types.h"

namespace z64 {

/// Number of RSP segment table slots. The selector is 4 bits wide.
inline constexpr u32 SegmentCount = 16;

/// A pointer as the game stores it: a segment selector plus a byte offset into
/// that segment. Segments stay separate buffers rather than one flat RDRAM, so
/// unpacking is how every parsed pointer becomes something addressable.
struct SegmentAddr {
    u8 segment;
    u32 offset;
};

[[nodiscard]] u32 pack(SegmentAddr address);

[[nodiscard]] SegmentAddr unpack(u32 raw);

/// Segment 0 is a direct address in the real console and is never a segment we
/// own, so it is the marker for "this pointer does not need resolving".
[[nodiscard]] bool is_segmented(u32 raw);

} // namespace z64
