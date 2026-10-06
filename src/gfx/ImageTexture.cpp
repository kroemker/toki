#include "ImageTexture.h"

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>

#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <utility>

namespace gfx {

namespace {

base::Diagnostic makeSdlError(const char* what) {
    return base::makeError({}, std::format("{}: {}", what, SDL_GetError()));
}

} // namespace

ImageTexture::ImageTexture(TextureHandle texture, const u32 width, const u32 height)
    : texture(std::move(texture)), width(width), height(height) {}

std::expected<ImageTexture, base::Diagnostic>
ImageTexture::create(const Device& device, const u32 width, const u32 height,
                     const std::span<const u8> pixels) {
    const usize size = usize{width} * height * 4;
    if (width == 0 || height == 0 || pixels.size() != size) {
        return std::unexpected(
            base::makeError({}, std::format("a {}x{} image needs {} bytes, not {}", width, height,
                                            size, pixels.size())));
    }
    SDL_GPUDevice* handle = device.getHandle();
    const SDL_GPUTextureCreateInfo info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    TextureHandle texture(handle, SDL_CreateGPUTexture(handle, &info));
    if (!texture) {
        return std::unexpected(makeSdlError("the image texture could not be created"));
    }
    const SDL_GPUTransferBufferCreateInfo transferInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<u32>(size),
        .props = 0,
    };
    const TransferBufferHandle transfer(handle, SDL_CreateGPUTransferBuffer(handle, &transferInfo));
    if (!transfer) {
        return std::unexpected(makeSdlError("the image upload buffer could not be created"));
    }
    void* mapped = SDL_MapGPUTransferBuffer(handle, transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected(makeSdlError("the image upload buffer could not be mapped"));
    }
    std::memcpy(mapped, pixels.data(), size);
    SDL_UnmapGPUTransferBuffer(handle, transfer.get());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(handle);
    if (commands == nullptr) {
        return std::unexpected(makeSdlError("no command buffer for the image upload"));
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo source{
        .transfer_buffer = transfer.get(),
        .offset = 0,
        .pixels_per_row = width,
        .rows_per_layer = height,
    };
    const SDL_GPUTextureRegion region{
        .texture = texture.get(),
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = width,
        .h = height,
        .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected(makeSdlError("the image upload could not be submitted"));
    }
    return ImageTexture(std::move(texture), width, height);
}

} // namespace gfx
