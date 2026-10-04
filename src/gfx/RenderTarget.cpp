#include "RenderTarget.h"

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>

#include <cstring>
#include <expected>
#include <format>
#include <utility>
#include <vector>

namespace gfx {

namespace {

base::Diagnostic makeSdlError(const char* what) {
    return base::makeError({}, std::format("{}: {}", what, SDL_GetError()));
}

TextureHandle makeTexture(SDL_GPUDevice* device, const SDL_GPUTextureFormat format,
                          const SDL_GPUTextureUsageFlags usage, const u32 width, const u32 height) {
    const SDL_GPUTextureCreateInfo info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = usage,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    return {device, SDL_CreateGPUTexture(device, &info)};
}

} // namespace

RenderTarget::RenderTarget(TextureHandle color, TextureHandle depth,
                           const SDL_GPUTextureFormat depthFormat, const u32 width,
                           const u32 height)
    : color(std::move(color)), depth(std::move(depth)), depthFormat(depthFormat), width(width),
      height(height) {}

std::expected<RenderTarget, base::Diagnostic>
RenderTarget::create(const Device& device, const u32 width, const u32 height) {
    SDL_GPUDevice* handle = device.getHandle();
    TextureHandle color = makeTexture(
        handle, ColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        width, height);
    if (!color) {
        return std::unexpected(makeSdlError("the colour target could not be created"));
    }
    const SDL_GPUTextureFormat depthFormat = device.getDepthFormat();
    TextureHandle depth =
        makeTexture(handle, depthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height);
    if (!depth) {
        return std::unexpected(makeSdlError("the depth target could not be created"));
    }
    return RenderTarget(std::move(color), std::move(depth), depthFormat, width, height);
}

std::expected<std::vector<u8>, base::Diagnostic>
RenderTarget::readPixels(const Device& device) const {
    SDL_GPUDevice* handle = device.getHandle();
    const u32 size = width * height * 4;
    const SDL_GPUTransferBufferCreateInfo transferInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = size,
        .props = 0,
    };
    const TransferBufferHandle transfer(handle, SDL_CreateGPUTransferBuffer(handle, &transferInfo));
    if (!transfer) {
        return std::unexpected(makeSdlError("the readback buffer could not be created"));
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(handle);
    if (commands == nullptr) {
        return std::unexpected(makeSdlError("no command buffer for the readback"));
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureRegion region{
        .texture = color.get(),
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = width,
        .h = height,
        .d = 1,
    };
    const SDL_GPUTextureTransferInfo destination{
        .transfer_buffer = transfer.get(),
        .offset = 0,
        .pixels_per_row = width,
        .rows_per_layer = height,
    };
    SDL_DownloadFromGPUTexture(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (fence == nullptr) {
        return std::unexpected(makeSdlError("the readback could not be submitted"));
    }
    SDL_WaitForGPUFences(handle, true, &fence, 1);
    SDL_ReleaseGPUFence(handle, fence);

    std::vector<u8> pixels(size);
    const void* mapped = SDL_MapGPUTransferBuffer(handle, transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected(makeSdlError("the readback buffer could not be mapped"));
    }
    std::memcpy(pixels.data(), mapped, size);
    SDL_UnmapGPUTransferBuffer(handle, transfer.get());
    return pixels;
}

} // namespace gfx
