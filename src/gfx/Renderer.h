#pragma once

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/DrawBatches.h"
#include "gfx/GpuHandle.h"
#include "gfx/Overlay.h"
#include "gfx/OverlayPipeline.h"
#include "gfx/OverlayVertices.h"
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

/// Draws what the RDP produced into a render target with the ubershader, and
/// lines over it with the overlay shader in the same pass. It
/// keeps a pointer to the device it was made from, which must outlive it, and
/// keeps its buffers between frames, growing them when a frame needs more.
class Renderer {
public:
    [[nodiscard]] static std::expected<Renderer, base::Diagnostic> create(const Device& device);

    /// Clears `target`, draws every primitive into it, and then `overlay`
    /// where the primitives do not hide it, and faintly where they do if it
    /// asks. The work is submitted, not waited
    /// for: reading the target back waits for it. Returns what could not be
    /// drawn faithfully.
    [[nodiscard]] std::vector<base::Diagnostic>
    render(const n64::RspResult& rspResult, const n64::RdpResult& rdpResult,
           const RenderTarget& target, ScreenSize screen = {}, const Overlay& overlay = {});

    /// How many draw calls the last `render` made.
    [[nodiscard]] usize getDrawCalls() const {
        return drawCalls;
    }

private:
    struct GrowableBuffer {
        BufferHandle buffer;
        u32 capacity = 0;
    };

    Renderer(const Device& device, ShaderHandle vertexShader, ShaderHandle fragmentShader,
             OverlayPipeline overlayPipeline);

    [[nodiscard]] std::expected<void, base::Diagnostic>
    upload(const std::vector<GpuVertex>& vertices,
           const std::vector<GpuOverlayVertex>& overlayVertices,
           const std::vector<n64::TextureMemoryImage>& textureMemories);
    void drawOverlay(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands,
                     const Overlay& overlay, const RenderTarget& target, ScreenSize screen,
                     u32 vertexCount);
    [[nodiscard]] bool reserve(GrowableBuffer& buffer, SDL_GPUBufferUsageFlags usage, u32 size);
    [[nodiscard]] bool reserveUpload(u32 size);

    const Device* device;
    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    std::unique_ptr<PipelineCache> pipelineCache;
    OverlayPipeline overlayPipeline;
    GrowableBuffer vertexBuffer;
    GrowableBuffer overlayBuffer;
    GrowableBuffer textureMemoryBuffer;
    TransferBufferHandle uploadBuffer;
    u32 uploadCapacity = 0;
    usize drawCalls = 0;
};

} // namespace gfx
