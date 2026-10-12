#pragma once

#include "n64/Color.h"
#include "n64/OtherMode.h"
#include "n64/Types.h"

#include <array>

namespace n64 {

/// What the blender reads for one pixel.
struct BlenderInputs {
    /// The combiner's output.
    Color pixel{};
    /// What the framebuffer already holds there.
    Color memory{};
    Color fog{};
    Color blend{};
    u8 shadeAlpha = 0;
};

/// Runs the blender as the RDP does, in its arithmetic: weights cut to five
/// bits, `1 - A` as the bitwise inverse, the first of two cycles shifted
/// rather than divided, and an 8-bit result that wraps rather than saturates.
///
/// Rasterized coverage is not emulated, so every pixel the blender sees is
/// taken as fully covered (one alpha left uncovered never reaches it): the
/// final cycle blends only under `FORCE_BL`, which is how an opaque
/// anti-aliased surface's interior behaves, and colour-on-coverage never
/// applies. The hardware's divider for the unforced final cycle, reached only
/// at a surface's edges, is therefore never needed.
[[nodiscard]] std::array<u8, 3> evaluateBlender(const OtherMode& otherMode,
                                                const BlenderInputs& inputs);

} // namespace n64
