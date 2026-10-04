#pragma once

#include "gfx/Types.h"

#include <span>

namespace gfx {

enum class ShaderFormat : u8 {
    Spirv,
    Dxil,
    Msl,
};

enum class ShaderStage : u8 {
    Vertex,
    Fragment,
};

/// The ubershader compiled for `format`. Each platform's build embeds only the
/// formats its backends read — DXIL and SPIR-V on Windows, MSL and SPIR-V on
/// Apple, SPIR-V elsewhere — and the rest come back empty.
[[nodiscard]] std::span<const u8> findShaderCode(ShaderFormat format, ShaderStage stage);

} // namespace gfx
