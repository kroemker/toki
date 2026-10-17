#include "OverlayPipeline.h"

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/GpuHandle.h"
#include "gfx/OverlayVertices.h"
#include "gfx/RenderTarget.h"
#include "gfx/ShaderCode.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <utility>

namespace gfx {

namespace {

base::Diagnostic makeSdlError(const char* what) {
    return base::makeError({}, std::format("{}: {}", what, SDL_GetError()));
}

PipelineHandle makePipeline(const Device& device, SDL_GPUShader* vertexShader,
                            SDL_GPUShader* fragmentShader, const SDL_GPUCompareOp compare) {
    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0,
        .pitch = sizeof(GpuOverlayVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    const std::array<SDL_GPUVertexAttribute, 4> attributes{{
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = offsetof(GpuOverlayVertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = offsetof(GpuOverlayVertex, other)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(GpuOverlayVertex, extrusion)},
        {.location = 3,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
         .offset = offsetof(GpuOverlayVertex, color)},
    }};
    const SDL_GPUColorTargetDescription colorTarget{
        .format = RenderTarget::ColorFormat,
        .blend_state =
            {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .color_write_mask = 0,
                .enable_blend = true,
                .enable_color_write_mask = false,
                .padding1 = 0,
                .padding2 = 0,
            },
    };
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader;
    info.fragment_shader = fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = compare;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = device.getDepthFormat();
    info.target_info.has_depth_stencil_target = true;
    return {device.getHandle(), SDL_CreateGPUGraphicsPipeline(device.getHandle(), &info)};
}

} // namespace

OverlayPipeline::OverlayPipeline(ShaderHandle vertexShader, ShaderHandle fragmentShader,
                                 PipelineHandle visible, PipelineHandle hidden)
    : vertexShader(std::move(vertexShader)), fragmentShader(std::move(fragmentShader)),
      visible(std::move(visible)), hidden(std::move(hidden)) {}

std::expected<OverlayPipeline, base::Diagnostic> OverlayPipeline::create(const Device& device) {
    ShaderHandle vertexShader =
        loadShader(device, Shader::Overlay, ShaderStage::Vertex, {.uniformBuffers = 1});
    if (!vertexShader) {
        return std::unexpected(makeSdlError("the overlay's vertex stage could not be loaded"));
    }
    ShaderHandle fragmentShader = loadShader(device, Shader::Overlay, ShaderStage::Fragment, {});
    if (!fragmentShader) {
        return std::unexpected(makeSdlError("the overlay's fragment stage could not be loaded"));
    }
    PipelineHandle visible = makePipeline(device, vertexShader.get(), fragmentShader.get(),
                                          SDL_GPU_COMPAREOP_LESS_OR_EQUAL);
    PipelineHandle hidden =
        makePipeline(device, vertexShader.get(), fragmentShader.get(), SDL_GPU_COMPAREOP_GREATER);
    if (!visible || !hidden) {
        return std::unexpected(makeSdlError("the overlay's pipelines could not be created"));
    }
    return OverlayPipeline(std::move(vertexShader), std::move(fragmentShader), std::move(visible),
                           std::move(hidden));
}

} // namespace gfx
