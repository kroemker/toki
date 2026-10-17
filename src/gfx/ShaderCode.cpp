#include "ShaderCode.h"

#include "gfx/Device.h"
#include "gfx/EmbeddedShaders.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <span>

namespace gfx {

namespace {

/// One shader's code in every format, vertex stage first.
struct ShaderBinaries {
    std::array<std::span<const u8>, 2> spirv;
    std::array<std::span<const u8>, 2> dxil;
    std::array<std::span<const u8>, 2> msl;
};

ShaderBinaries findBinaries(const Shader shader) {
    switch (shader) {
    case Shader::Ubershader:
        return {.spirv = {shaders::UbershaderVertexSpirv, shaders::UbershaderFragmentSpirv},
                .dxil = {shaders::UbershaderVertexDxil, shaders::UbershaderFragmentDxil},
                .msl = {shaders::UbershaderVertexMsl, shaders::UbershaderFragmentMsl}};
    case Shader::Overlay:
        return {.spirv = {shaders::OverlayVertexSpirv, shaders::OverlayFragmentSpirv},
                .dxil = {shaders::OverlayVertexDxil, shaders::OverlayFragmentDxil},
                .msl = {shaders::OverlayVertexMsl, shaders::OverlayFragmentMsl}};
    }
    return {};
}

SDL_GPUShaderFormat findSdlFormat(const ShaderFormat format) {
    switch (format) {
    case ShaderFormat::Spirv:
        return SDL_GPU_SHADERFORMAT_SPIRV;
    case ShaderFormat::Dxil:
        return SDL_GPU_SHADERFORMAT_DXIL;
    case ShaderFormat::Msl:
        return SDL_GPU_SHADERFORMAT_MSL;
    }
    return SDL_GPU_SHADERFORMAT_INVALID;
}

} // namespace

std::span<const u8> findShaderCode(const Shader shader, const ShaderFormat format,
                                   const ShaderStage stage) {
    const ShaderBinaries binaries = findBinaries(shader);
    const usize index = stage == ShaderStage::Vertex ? 0 : 1;
    switch (format) {
    case ShaderFormat::Spirv:
        return binaries.spirv[index];
    case ShaderFormat::Dxil:
        return binaries.dxil[index];
    case ShaderFormat::Msl:
        return binaries.msl[index];
    }
    return {};
}

ShaderHandle loadShader(const Device& device, const Shader shader, const ShaderStage stage,
                        const ShaderBindings& bindings) {
    const bool vertex = stage == ShaderStage::Vertex;
    const std::span<const u8> code = findShaderCode(shader, device.getShaderFormat(), stage);
    const SDL_GPUShaderCreateInfo info{
        .code_size = code.size(),
        .code = code.data(),
        .entrypoint = vertex ? "VertexMain" : "FragmentMain",
        .format = findSdlFormat(device.getShaderFormat()),
        .stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = bindings.storageBuffers,
        .num_uniform_buffers = bindings.uniformBuffers,
        .props = 0,
    };
    return {device.getHandle(), SDL_CreateGPUShader(device.getHandle(), &info)};
}

} // namespace gfx
