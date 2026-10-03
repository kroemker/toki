#pragma once

#include "n64/Types.h"

namespace n64 {

inline constexpr usize SegmentCount = 16;

struct SegmentAddress {
    u8 segment;
    u32 offset;
};

[[nodiscard]] u32 pack(SegmentAddress address);

[[nodiscard]] SegmentAddress unpack(u32 raw);

[[nodiscard]] bool isSegmented(u32 raw);

} // namespace n64
