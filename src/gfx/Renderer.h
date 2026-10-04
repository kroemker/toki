#pragma once

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/DrawBatches.h"
#include "gfx/GpuHandle.h"
#include "gfx/PipelineCache.h"
#include "gfx/RenderTarget.h"
#include "gfx/Types.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <memory>
#include <vector>

namespace gfx {

/// Draws what the RDP produced into a render target with the ubershader. It
/// keeps a pointer to the device it was made from, which must outlive it, and
/// keeps its buffers between frames, growing them when a frame needs more.
class Renderer {
public:
    [[nodiscard]] static std::expected<Renderer, base::Diagnostic> create(const Device& device);

    /// Clears `target` and draws every primitive into it. The work is
    /// submitted, not waited for: reading the target back waits for it.
    /// Returns what could not be drawn faithfully.
    [[nodiscard]] std::vector<base::Diagnostic> render(const n64::RspResult& rspResult,
                                                       const n64::RdpResult& rdpResult,
                                                       const RenderTarget& target,
                                                       ScreenSize screen = {});

private:
    struct GrowableBuffer {
        BufferHandle buffer;
        u32 capacity = 0;
    };

    Renderer(const Device& device, ShaderHandle vertexShader, ShaderHandle fragmentShader);

    [[nodiscard]] std::expected<void, base::Diagnostic>
    upload(const std::vector<GpuVertex>& vertices,
           const std::vector<n64::TextureMemoryImage>& textureMemories);
    [[nodiscard]] bool reserve(GrowableBuffer& buffer, SDL_GPUBufferUsageFlags usage, u32 size);
    [[nodiscard]] bool reserveUpload(u32 size);

    const Device* device;
    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    std::unique_ptr<PipelineCache> pipelineCache;
    GrowableBuffer vertexBuffer;
    GrowableBuffer textureMemoryBuffer;
    TransferBufferHandle uploadBuffer;
    u32 uploadCapacity = 0;
};

} // namespace gfx
