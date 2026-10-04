#pragma once

#include "n64/Types.h"

#include <array>

namespace n64 {

/// Every input the colour combiner can select, under one name each. A
/// selector's meaning depends on the slot it sits in, so `makeCombineMode`
/// translates the slot-relative numbers of `G_SETCOMBINE` into these. An input
/// named `...Alpha` reads the alpha of its source on every channel.
enum class CombinerInput : u8 {
    Combined,
    Texel0,
    Texel1,
    Primitive,
    Shade,
    Environment,
    One,
    Zero,
    Noise,
    KeyCenter,
    KeyScale,
    ConvertK4,
    ConvertK5,
    CombinedAlpha,
    Texel0Alpha,
    Texel1Alpha,
    PrimitiveAlpha,
    ShadeAlpha,
    EnvironmentAlpha,
    LodFraction,
    PrimitiveLodFraction,
};

/// One cycle of the combiner: `(a - b) * c + d`, once for colour and once for
/// alpha, each as the four inputs `a`, `b`, `c`, `d`.
struct CombinerCycle {
    std::array<CombinerInput, 4> color{};
    std::array<CombinerInput, 4> alpha{};

    friend bool operator==(const CombinerCycle&, const CombinerCycle&) = default;
};

/// `G_SETCOMBINE` decoded. One-cycle mode combines with cycle 1, not cycle 0.
struct CombineMode {
    std::array<CombinerCycle, 2> cycles{};

    friend bool operator==(const CombineMode&, const CombineMode&) = default;
};

[[nodiscard]] CombineMode makeCombineMode(u32 word0, u32 word1);

} // namespace n64
