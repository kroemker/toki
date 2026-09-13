#pragma once

#include "z64/Types.h"

namespace z64 {

inline constexpr u32 SegmentCount = 16;

struct SegmentAddr {
    u8 segment;
    u32 offset;
};

[[nodiscard]] u32 pack(SegmentAddr address);

[[nodiscard]] SegmentAddr unpack(u32 raw);

[[nodiscard]] bool isSegmented(u32 raw);

} // namespace z64
