#include "segment.h"

namespace z64 {

namespace {
constexpr u32 OffsetMask = 0x00FFFFFF;
constexpr u32 SegmentShift = 24;
constexpr u32 SegmentMask = 0x0F;
} // namespace

u32 pack(SegmentAddr address) {
    return (static_cast<u32>(address.segment & SegmentMask) << SegmentShift) |
           (address.offset & OffsetMask);
}

SegmentAddr unpack(u32 raw) {
    return SegmentAddr{
        .segment = static_cast<u8>((raw >> SegmentShift) & SegmentMask),
        .offset = raw & OffsetMask,
    };
}

bool is_segmented(u32 raw) {
    return ((raw >> SegmentShift) & SegmentMask) != 0;
}

} // namespace z64
