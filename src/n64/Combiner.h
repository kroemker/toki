#pragma once

#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"
#include "n64/Types.h"

#include <array>

namespace n64 {

/// What the combiner reads for one pixel.
struct CombinerInputs {
    Color texel0{};
    Color texel1{};
    Color primitive{};
    Color shade{};
    Color environment{};
    u8 lodFraction = 0;
    u8 primitiveLodFraction = 0;
    /// The combiner's noise input as the RDP forms it: three random bits
    /// shifted up by six, with 0x20 added.
    u16 noise = 0x20;
    std::array<u8, 3> keyCenter{};
    std::array<u8, 3> keyScale{};
    /// The 9-bit `G_SETCONVERT` constants K4 and K5.
    u16 convertK4 = 0;
    u16 convertK5 = 0;
    /// Covered subpixels, out of eight.
    u8 coverage = 8;
};

struct CombinerResult {
    Color color{};
    /// The alpha the alpha compare tests: the first cycle's in two-cycle mode.
    u8 alphaReference = 0;
};

/// Runs the colour combiner as the RDP does, in its 9-bit signed arithmetic:
/// the inputs of `(a - b) * c + d` are sign-extended the way the hardware's
/// adders treat them, the second cycle reads the first cycle's unclamped
/// result as `Combined` and its texels swapped, and the result is clamped and
/// its alpha modulated by coverage when the other mode asks for it. Alpha
/// dither is not applied.
[[nodiscard]] CombinerResult evaluateCombiner(const CombineMode& combineMode,
                                              const OtherMode& otherMode,
                                              const CombinerInputs& inputs);

/// Whether a pixel survives the alpha compare. `threshold` is the blend
/// colour's alpha, or the random threshold when the compare dithers.
[[nodiscard]] bool passesAlphaCompare(const OtherMode& otherMode, u8 alphaReference, u8 threshold);

} // namespace n64
