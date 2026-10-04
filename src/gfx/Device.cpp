#include "Device.h"

#include "base/Diagnostic.h"
#include "gfx/ShaderCode.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>

#include <array>
#include <expected>
#include <format>
#include <memory>
#include <utility>

namespace gfx {

namespace {

struct FormatChoice {
    ShaderFormat format;
    SDL_GPUShaderFormat sdlFormat;
};

constexpr std::array<FormatChoice, 3> FormatPreference{{
    {.format = ShaderFormat::Dxil, .sdlFormat = SDL_GPU_SHADERFORMAT_DXIL},
    {.format = ShaderFormat::Msl, .sdlFormat = SDL_GPU_SHADERFORMAT_MSL},
    {.format = ShaderFormat::Spirv, .sdlFormat = SDL_GPU_SHADERFORMAT_SPIRV},
}};

bool isEmbedded(const ShaderFormat format) {
    return !findShaderCode(format, ShaderStage::Vertex).empty() &&
           !findShaderCode(format, ShaderStage::Fragment).empty();
}

SDL_GPUTextureFormat findDepthFormat(SDL_GPUDevice* device) {
    constexpr std::array<SDL_GPUTextureFormat, 3> Candidates{
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };
    for (const SDL_GPUTextureFormat format : Candidates) {
        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            return format;
        }
    }
    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

} // namespace

void Device::Destroyer::operator()(SDL_GPUDevice* device) const {
    SDL_DestroyGPUDevice(device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

Device::Device(std::unique_ptr<SDL_GPUDevice, Destroyer> device, const ShaderFormat shaderFormat,
               const SDL_GPUTextureFormat depthFormat)
    : device(std::move(device)), shaderFormat(shaderFormat), depthFormat(depthFormat) {}

std::expected<Device, base::Diagnostic> Device::create() {
    SDL_GPUShaderFormat offered = SDL_GPU_SHADERFORMAT_INVALID;
    for (const FormatChoice& choice : FormatPreference) {
        if (isEmbedded(choice.format)) {
            offered |= choice.sdlFormat;
        }
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return std::unexpected(base::makeError(
            {}, std::format("SDL's video subsystem could not start: {}", SDL_GetError())));
    }
    std::unique_ptr<SDL_GPUDevice, Destroyer> device(SDL_CreateGPUDevice(offered, false, nullptr));
    if (!device) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return std::unexpected(
            base::makeError({}, std::format("no GPU device could be created: {}", SDL_GetError())));
    }
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device.get());
    ShaderFormat format = ShaderFormat::Spirv;
    for (const FormatChoice& choice : FormatPreference) {
        if ((supported & offered & choice.sdlFormat) != 0) {
            format = choice.format;
            break;
        }
    }
    const SDL_GPUTextureFormat depthFormat = findDepthFormat(device.get());
    return Device(std::move(device), format, depthFormat);
}

} // namespace gfx
