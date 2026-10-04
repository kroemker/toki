#pragma once

#include "n64/Types.h"

#include <array>

namespace n64 {

/// Red, green, blue and alpha, eight bits each, as the RDP's colour
/// registers and texel decoders produce them.
using Color = std::array<u8, 4>;

} // namespace n64
