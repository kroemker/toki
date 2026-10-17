#pragma once

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"

#include <SDL3/SDL_gpu.h>

#include <expected>

namespace gfx {

/// Which part of a line the overlay draws: what is in front of the frame's
/// geometry, or what it hides.
enum class OverlayDepth : u8 {
    Visible,
    Hidden,
};

/// The overlay shader and its two pipelines, both blending over the frame
/// and leaving its depth as it is.
class OverlayPipeline {
public:
    [[nodiscard]] static std::expected<OverlayPipeline, base::Diagnostic>
    create(const Device& device);

    [[nodiscard]] SDL_GPUGraphicsPipeline* getPipeline(const OverlayDepth depth) const {
        return depth == OverlayDepth::Visible ? visible.get() : hidden.get();
    }

private:
    OverlayPipeline(ShaderHandle vertexShader, ShaderHandle fragmentShader, PipelineHandle visible,
                    PipelineHandle hidden);

    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    PipelineHandle visible;
    PipelineHandle hidden;
};

} // namespace gfx
