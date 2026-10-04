#pragma once

#include "gfx/DrawBatches.h"
#include "gfx/GpuHandle.h"
#include "gfx/Types.h"

#include <SDL3/SDL_gpu.h>

#include <utility>
#include <vector>

namespace gfx {

/// The ubershader's pipelines, one per `PipelineKey` a frame has needed. SDL
/// bakes blend and depth state into a pipeline, so these few settings are all
/// that ever varies.
class PipelineCache {
public:
    PipelineCache(SDL_GPUDevice* device, SDL_GPUShader* vertexShader, SDL_GPUShader* fragmentShader,
                  SDL_GPUTextureFormat depthFormat);

    /// The pipeline for `key`, created on first use; null if SDL refuses it.
    [[nodiscard]] SDL_GPUGraphicsPipeline* findPipeline(const PipelineKey& key);

private:
    SDL_GPUDevice* device;
    SDL_GPUShader* vertexShader;
    SDL_GPUShader* fragmentShader;
    SDL_GPUTextureFormat depthFormat;
    std::vector<std::pair<PipelineKey, PipelineHandle>> pipelines;
};

} // namespace gfx
