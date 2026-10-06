#pragma once

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <span>

namespace gfx {

/// A picture for the UI to show: 8-bit RGBA pixels uploaded once into a
/// texture ImGui can draw.
class ImageTexture {
public:
    /// Uploads `pixels`, four bytes a pixel, row by row from the top.
    [[nodiscard]] static std::expected<ImageTexture, base::Diagnostic>
    create(const Device& device, u32 width, u32 height, std::span<const u8> pixels);

    [[nodiscard]] SDL_GPUTexture* getTexture() const {
        return texture.get();
    }

    [[nodiscard]] u32 getWidth() const {
        return width;
    }

    [[nodiscard]] u32 getHeight() const {
        return height;
    }

private:
    ImageTexture(TextureHandle texture, u32 width, u32 height);

    TextureHandle texture;
    u32 width;
    u32 height;
};

} // namespace gfx
