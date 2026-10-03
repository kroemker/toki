#pragma once

#include "n64/Types.h"

#include <span>

namespace n64 {

[[nodiscard]] inline u16 readU16(const std::span<const u8> bytes, const usize offset) {
    return static_cast<u16>((bytes[offset] << 8) | bytes[offset + 1]);
}

[[nodiscard]] inline s16 readS16(const std::span<const u8> bytes, const usize offset) {
    return static_cast<s16>(readU16(bytes, offset));
}

[[nodiscard]] inline u32 readU32(const std::span<const u8> bytes, const usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | static_cast<u32>(bytes[offset + 3]);
}

} // namespace n64
