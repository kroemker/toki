#pragma once

#include "base/Diagnostic.h"
#include "gfx/BlendSetup.h"
#include "gfx/ShaderUniforms.h"
#include "gfx/Types.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"

#include <array>
#include <vector>

namespace gfx {

/// One vertex as the ubershader reads it.
struct GpuVertex {
    std::array<f32, 4> position{};
    std::array<f32, 2> texture{};
    std::array<u8, 4> color{};
};

static_assert(sizeof(GpuVertex) == 28, "GpuVertex must match the pipeline's vertex attributes");

enum class DepthCompare : u8 {
    Always,
    Less,
    LessOrEqual,
};

/// Everything about a draw that SDL bakes into a pipeline.
struct PipelineKey {
    BlendSetup blend;
    bool depthTest = false;
    bool depthWrite = false;
    DepthCompare depthCompare = DepthCompare::Less;

    friend bool operator==(const PipelineKey&, const PipelineKey&) = default;
};

/// The N64 screen the display list draws to. Primitives are placed in its
/// pixels; the render target can be any size.
struct ScreenSize {
    u32 width = 320;
    u32 height = 240;
};

/// A run of consecutive primitives drawn with one draw call.
struct DrawBatch {
    u32 firstVertex = 0;
    u32 vertexCount = 0;
    VertexUniforms vertexUniforms;
    FragmentUniforms fragmentUniforms;
    PipelineKey pipeline;
    /// Left, top, right, bottom, in screen pixels.
    std::array<s32, 4> scissor{};
};

struct DrawBatches {
    std::vector<GpuVertex> vertices;
    std::vector<DrawBatch> batches;
    std::vector<base::Diagnostic> diagnostics;
};

/// Turns what the RDP draws into vertices and draw calls. Consecutive
/// primitives that need the same uniforms, pipeline and scissor share a call.
/// A blender the GPU cannot reproduce is reported once per state and drawn
/// without blending; a fill rectangle into the depth buffer is skipped,
/// because the target's depth is cleared before every frame.
[[nodiscard]] DrawBatches makeDrawBatches(const n64::RspResult& rspResult,
                                          const n64::RdpResult& rdpResult, ScreenSize screen);

} // namespace gfx
