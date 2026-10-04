#pragma once

#include "gfx/Types.h"
#include "n64/OtherMode.h"

#include <expected>
#include <string>

namespace gfx {

enum class BlendFactor : u8 {
    Zero,
    One,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationAlpha,
};

/// What the shader outputs for the final blender cycle, and so what the
/// fixed-function blend does with it.
enum class BlendPath : u8 {
    /// The cycle reads no framebuffer: the shader computes it exactly.
    Shader = 0,
    /// `P * A + memory * B`: the shader outputs `P` with `A` as alpha.
    OverMemory = 1,
    /// `memory * A + M * (1 - A)`: the shader outputs `M` with `1 - A` as alpha.
    MemoryUnder = 2,
    /// The unforced cycle passes memory through: nothing is written.
    KeepMemory = 3,
};

struct BlendSetup {
    BlendPath path = BlendPath::Shader;
    bool enabled = false;
    BlendFactor source = BlendFactor::One;
    BlendFactor destination = BlendFactor::Zero;

    friend bool operator==(const BlendSetup&, const BlendSetup&) = default;
};

/// Maps the other mode's blender onto the GPU. Cycles that read no memory run
/// in the shader; a final cycle that reads memory becomes a fixed-function
/// blend when it has one. A blender with no equivalent — memory weighed against
/// memory, or memory in the first of two cycles — is an error naming why.
[[nodiscard]] std::expected<BlendSetup, std::string>
makeBlendSetup(const n64::OtherMode& otherMode);

} // namespace gfx
