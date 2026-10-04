#pragma once

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <vector>

namespace gfx {

/// An offscreen colour and depth buffer to draw into. Colour is 8-bit RGBA.
class RenderTarget {
public:
    static constexpr SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    [[nodiscard]] static std::expected<RenderTarget, base::Diagnostic>
    create(const Device& device, u32 width, u32 height);

    [[nodiscard]] SDL_GPUTexture* getColor() const {
        return color.get();
    }

    [[nodiscard]] SDL_GPUTexture* getDepth() const {
        return depth.get();
    }

    [[nodiscard]] SDL_GPUTextureFormat getDepthFormat() const {
        return depthFormat;
    }

    [[nodiscard]] u32 getWidth() const {
        return width;
    }

    [[nodiscard]] u32 getHeight() const {
        return height;
    }

    /// Copies the colour buffer back to the CPU, row by row from the top, four
    /// bytes a pixel. Waits for the GPU.
    [[nodiscard]] std::expected<std::vector<u8>, base::Diagnostic>
    readPixels(const Device& device) const;

private:
    RenderTarget(TextureHandle color, TextureHandle depth, SDL_GPUTextureFormat depthFormat,
                 u32 width, u32 height);

    TextureHandle color;
    TextureHandle depth;
    SDL_GPUTextureFormat depthFormat;
    u32 width;
    u32 height;
};

} // namespace gfx
