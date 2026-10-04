#include "PipelineCache.h"

#include "gfx/BlendSetup.h"
#include "gfx/DrawBatches.h"
#include "gfx/GpuHandle.h"
#include "gfx/RenderTarget.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <utility>

namespace gfx {

namespace {

SDL_GPUBlendFactor findBlendFactor(const BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return SDL_GPU_BLENDFACTOR_ZERO;
    case BlendFactor::One:
        return SDL_GPU_BLENDFACTOR_ONE;
    case BlendFactor::SourceAlpha:
        return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:
        return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUCompareOp findCompareOp(const DepthCompare compare) {
    switch (compare) {
    case DepthCompare::Always:
        return SDL_GPU_COMPAREOP_ALWAYS;
    case DepthCompare::Less:
        return SDL_GPU_COMPAREOP_LESS;
    case DepthCompare::LessOrEqual:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

} // namespace

PipelineCache::PipelineCache(SDL_GPUDevice* device, SDL_GPUShader* vertexShader,
                             SDL_GPUShader* fragmentShader, const SDL_GPUTextureFormat depthFormat)
    : device(device), vertexShader(vertexShader), fragmentShader(fragmentShader),
      depthFormat(depthFormat) {}

SDL_GPUGraphicsPipeline* PipelineCache::findPipeline(const PipelineKey& key) {
    for (const auto& [cachedKey, pipeline] : pipelines) {
        if (cachedKey == key) {
            return pipeline.get();
        }
    }

    const SDL_GPUVertexBufferDescription buffer{
        .slot = 0,
        .pitch = sizeof(GpuVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    const std::array<SDL_GPUVertexAttribute, 3> attributes{{
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = offsetof(GpuVertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = offsetof(GpuVertex, texture)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
         .offset = offsetof(GpuVertex, color)},
    }};
    const SDL_GPUBlendFactor source = findBlendFactor(key.blend.source);
    const SDL_GPUBlendFactor destination = findBlendFactor(key.blend.destination);
    const SDL_GPUColorTargetDescription colorTarget{
        .format = RenderTarget::ColorFormat,
        .blend_state =
            {
                .src_color_blendfactor = source,
                .dst_color_blendfactor = destination,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .color_write_mask = 0,
                .enable_blend = key.blend.enabled,
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
    info.depth_stencil_state.compare_op = findCompareOp(key.depthCompare);
    info.depth_stencil_state.enable_depth_test = key.depthTest;
    info.depth_stencil_state.enable_depth_write = key.depthWrite;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depthFormat;
    info.target_info.has_depth_stencil_target = true;

    PipelineHandle pipeline(device, SDL_CreateGPUGraphicsPipeline(device, &info));
    SDL_GPUGraphicsPipeline* created = pipeline.get();
    if (created != nullptr) {
        pipelines.emplace_back(key, std::move(pipeline));
    }
    return created;
}

} // namespace gfx
