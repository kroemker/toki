#pragma once

#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <span>

namespace gfx {

class Device;

enum class ShaderFormat : u8 {
    Spirv,
    Dxil,
    Msl,
};

enum class ShaderStage : u8 {
    Vertex,
    Fragment,
};

/// Each shader under `gfx/shaders/`.
enum class Shader : u8 {
    /// Draws what the RDP draws.
    Ubershader,
    /// Draws lines over the frame.
    Overlay,
};

/// `shader` compiled for `format`. Each platform's build embeds only the
/// formats its backends read — DXIL and SPIR-V on Windows, MSL and SPIR-V on
/// Apple, SPIR-V elsewhere — and the rest come back empty.
[[nodiscard]] std::span<const u8> findShaderCode(Shader shader, ShaderFormat format,
                                                 ShaderStage stage);

/// The resources one stage of a shader binds.
struct ShaderBindings {
    u32 storageBuffers = 0;
    u32 uniformBuffers = 0;
};

/// One stage of `shader` created on `device` in the format it reads; empty
/// when SDL refuses it, with the reason in `SDL_GetError`.
[[nodiscard]] ShaderHandle loadShader(const Device& device, Shader shader, ShaderStage stage,
                                      const ShaderBindings& bindings);

} // namespace gfx
