#pragma once

#include "base/Diagnostic.h"
#include "gfx/ShaderCode.h"
#include "gfx/Types.h"

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <memory>

namespace gfx {

/// The SDL GPU device everything in `gfx/` draws with: D3D12, Vulkan or
/// Metal, whichever SDL picks among those this build has shaders for. The
/// device also settles the depth format, so that every render target and every
/// pipeline made from it agree.
class Device {
public:
    /// Creates a device with no window, starting SDL's video subsystem for as
    /// long as the device lives. A window that is to show what the device
    /// draws is claimed for it afterwards. Fails with SDL's reason when there
    /// is no usable GPU backend, which tests treat as a reason to skip.
    [[nodiscard]] static std::expected<Device, base::Diagnostic> create();

    [[nodiscard]] SDL_GPUDevice* getHandle() const {
        return device.get();
    }

    [[nodiscard]] ShaderFormat getShaderFormat() const {
        return shaderFormat;
    }

    [[nodiscard]] SDL_GPUTextureFormat getDepthFormat() const {
        return depthFormat;
    }

private:
    struct Destroyer {
        void operator()(SDL_GPUDevice* device) const;
    };

    Device(std::unique_ptr<SDL_GPUDevice, Destroyer> device, ShaderFormat shaderFormat,
           SDL_GPUTextureFormat depthFormat);

    std::unique_ptr<SDL_GPUDevice, Destroyer> device;
    ShaderFormat shaderFormat;
    SDL_GPUTextureFormat depthFormat;
};

} // namespace gfx
