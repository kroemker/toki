#pragma once

#include <SDL3/SDL_gpu.h>

#include <utility>

namespace gfx {

/// Owns one SDL GPU object and releases it through the device that made it.
/// SDL's release functions all take the device, which a plain `unique_ptr`
/// deleter cannot carry without state, hence the handle.
template <typename Object, void (*Release)(SDL_GPUDevice*, Object*)>
class GpuHandle {
public:
    GpuHandle() = default;

    GpuHandle(SDL_GPUDevice* device, Object* object) : device(device), object(object) {}

    GpuHandle(const GpuHandle&) = delete;
    GpuHandle& operator=(const GpuHandle&) = delete;

    GpuHandle(GpuHandle&& other) noexcept
        : device(std::exchange(other.device, nullptr)),
          object(std::exchange(other.object, nullptr)) {}

    GpuHandle& operator=(GpuHandle&& other) noexcept {
        if (this != &other) {
            reset();
            device = std::exchange(other.device, nullptr);
            object = std::exchange(other.object, nullptr);
        }
        return *this;
    }

    ~GpuHandle() {
        reset();
    }

    [[nodiscard]] Object* get() const {
        return object;
    }

    [[nodiscard]] explicit operator bool() const {
        return object != nullptr;
    }

    void reset() {
        if (object != nullptr) {
            Release(device, object);
            object = nullptr;
        }
    }

private:
    SDL_GPUDevice* device = nullptr;
    Object* object = nullptr;
};

using TextureHandle = GpuHandle<SDL_GPUTexture, SDL_ReleaseGPUTexture>;
using BufferHandle = GpuHandle<SDL_GPUBuffer, SDL_ReleaseGPUBuffer>;
using TransferBufferHandle = GpuHandle<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer>;
using ShaderHandle = GpuHandle<SDL_GPUShader, SDL_ReleaseGPUShader>;
using PipelineHandle = GpuHandle<SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline>;

} // namespace gfx
