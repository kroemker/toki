#include "Renderer.h"

#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/DrawBatches.h"
#include "gfx/GpuHandle.h"
#include "gfx/PipelineCache.h"
#include "gfx/RenderTarget.h"
#include "gfx/ShaderCode.h"
#include "gfx/Types.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/TextureMemory.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_rect.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gfx {

namespace {

base::Diagnostic makeSdlError(const char* what) {
    return base::makeError({}, std::format("{}: {}", what, SDL_GetError()));
}

SDL_GPUShaderFormat findSdlFormat(const ShaderFormat format) {
    switch (format) {
    case ShaderFormat::Spirv:
        return SDL_GPU_SHADERFORMAT_SPIRV;
    case ShaderFormat::Dxil:
        return SDL_GPU_SHADERFORMAT_DXIL;
    case ShaderFormat::Msl:
        return SDL_GPU_SHADERFORMAT_MSL;
    }
    return SDL_GPU_SHADERFORMAT_INVALID;
}

ShaderHandle loadShader(const Device& device, const ShaderStage stage) {
    const bool vertex = stage == ShaderStage::Vertex;
    const std::span<const u8> code = findShaderCode(device.getShaderFormat(), stage);
    const SDL_GPUShaderCreateInfo info{
        .code_size = code.size(),
        .code = code.data(),
        .entrypoint = vertex ? "VertexMain" : "FragmentMain",
        .format = findSdlFormat(device.getShaderFormat()),
        .stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = vertex ? 0U : 1U,
        .num_uniform_buffers = 1,
        .props = 0,
    };
    return {device.getHandle(), SDL_CreateGPUShader(device.getHandle(), &info)};
}

SDL_Rect scaleScissor(const std::array<s32, 4>& scissor, const ScreenSize screen,
                      const RenderTarget& target) {
    const auto scaleX = [&](const s32 x) {
        return static_cast<int>(
            static_cast<s64>(std::clamp<s32>(x, 0, static_cast<s32>(screen.width))) *
            target.getWidth() / screen.width);
    };
    const auto scaleY = [&](const s32 y) {
        return static_cast<int>(
            static_cast<s64>(std::clamp<s32>(y, 0, static_cast<s32>(screen.height))) *
            target.getHeight() / screen.height);
    };
    const int left = scaleX(scissor[0]);
    const int top = scaleY(scissor[1]);
    return SDL_Rect{.x = left,
                    .y = top,
                    .w = std::max(scaleX(scissor[2]) - left, 0),
                    .h = std::max(scaleY(scissor[3]) - top, 0)};
}

SDL_GPURenderPass* beginFrame(SDL_GPUCommandBuffer* commands, const RenderTarget& target) {
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = target.getColor();
    colorTarget.clear_color = SDL_FColor{.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 0.0F};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = target.getDepth();
    depthTarget.clear_depth = 1.0F;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, &depthTarget);
    const SDL_GPUViewport viewport{.x = 0.0F,
                                   .y = 0.0F,
                                   .w = static_cast<float>(target.getWidth()),
                                   .h = static_cast<float>(target.getHeight()),
                                   .min_depth = 0.0F,
                                   .max_depth = 1.0F};
    SDL_SetGPUViewport(pass, &viewport);
    return pass;
}

} // namespace

Renderer::Renderer(const Device& device, ShaderHandle vertexShader, ShaderHandle fragmentShader)
    : device(&device), vertexShader(std::move(vertexShader)),
      fragmentShader(std::move(fragmentShader)),
      pipelineCache(std::make_unique<PipelineCache>(device.getHandle(), this->vertexShader.get(),
                                                    this->fragmentShader.get(),
                                                    device.getDepthFormat())) {}

std::expected<Renderer, base::Diagnostic> Renderer::create(const Device& device) {
    ShaderHandle vertexShader = loadShader(device, ShaderStage::Vertex);
    if (!vertexShader) {
        return std::unexpected(makeSdlError("the ubershader's vertex stage could not be loaded"));
    }
    ShaderHandle fragmentShader = loadShader(device, ShaderStage::Fragment);
    if (!fragmentShader) {
        return std::unexpected(makeSdlError("the ubershader's fragment stage could not be loaded"));
    }
    return Renderer(device, std::move(vertexShader), std::move(fragmentShader));
}

bool Renderer::reserve(GrowableBuffer& buffer, const SDL_GPUBufferUsageFlags usage,
                       const u32 size) {
    if (buffer.buffer && buffer.capacity >= size) {
        return true;
    }
    const u32 capacity = std::max(size, buffer.capacity * 2);
    const SDL_GPUBufferCreateInfo info{.usage = usage, .size = capacity, .props = 0};
    BufferHandle created(device->getHandle(), SDL_CreateGPUBuffer(device->getHandle(), &info));
    if (!created) {
        return false;
    }
    buffer = GrowableBuffer{.buffer = std::move(created), .capacity = capacity};
    return true;
}

bool Renderer::reserveUpload(const u32 size) {
    if (uploadBuffer && uploadCapacity >= size) {
        return true;
    }
    const u32 capacity = std::max(size, uploadCapacity * 2);
    const SDL_GPUTransferBufferCreateInfo info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = capacity, .props = 0};
    TransferBufferHandle created(device->getHandle(),
                                 SDL_CreateGPUTransferBuffer(device->getHandle(), &info));
    if (!created) {
        return false;
    }
    uploadBuffer = std::move(created);
    uploadCapacity = capacity;
    return true;
}

std::expected<void, base::Diagnostic>
Renderer::upload(const std::vector<GpuVertex>& vertices,
                 const std::vector<n64::TextureMemoryImage>& textureMemories) {
    SDL_GPUDevice* handle = device->getHandle();
    const auto vertexBytes = static_cast<u32>(vertices.size() * sizeof(GpuVertex));
    const usize memoryCount = std::max<usize>(textureMemories.size(), 1);
    const auto memoryBytes = static_cast<u32>(memoryCount * n64::TextureMemorySize);
    if (!reserve(vertexBuffer, SDL_GPU_BUFFERUSAGE_VERTEX, std::max<u32>(vertexBytes, 4)) ||
        !reserve(textureMemoryBuffer, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, memoryBytes) ||
        !reserveUpload(vertexBytes + memoryBytes)) {
        return std::unexpected(makeSdlError("the frame's buffers could not be created"));
    }
    auto* mapped = static_cast<u8*>(SDL_MapGPUTransferBuffer(handle, uploadBuffer.get(), true));
    if (mapped == nullptr) {
        return std::unexpected(makeSdlError("the frame's upload buffer could not be mapped"));
    }
    std::memcpy(mapped, vertices.data(), vertexBytes);
    std::memset(mapped + vertexBytes, 0, memoryBytes);
    for (usize i = 0; i < textureMemories.size(); ++i) {
        std::memcpy(mapped + vertexBytes + (i * n64::TextureMemorySize), textureMemories[i].data(),
                    n64::TextureMemorySize);
    }
    SDL_UnmapGPUTransferBuffer(handle, uploadBuffer.get());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(handle);
    if (commands == nullptr) {
        return std::unexpected(makeSdlError("no command buffer for the upload"));
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    if (vertexBytes > 0) {
        const SDL_GPUTransferBufferLocation source{.transfer_buffer = uploadBuffer.get(),
                                                   .offset = 0};
        const SDL_GPUBufferRegion region{
            .buffer = vertexBuffer.buffer.get(), .offset = 0, .size = vertexBytes};
        SDL_UploadToGPUBuffer(copy, &source, &region, true);
    }
    const SDL_GPUTransferBufferLocation memorySource{.transfer_buffer = uploadBuffer.get(),
                                                     .offset = vertexBytes};
    const SDL_GPUBufferRegion memoryRegion{
        .buffer = textureMemoryBuffer.buffer.get(), .offset = 0, .size = memoryBytes};
    SDL_UploadToGPUBuffer(copy, &memorySource, &memoryRegion, true);
    SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected(makeSdlError("the upload could not be submitted"));
    }
    return {};
}

std::vector<base::Diagnostic> Renderer::render(const n64::RspResult& rspResult,
                                               const n64::RdpResult& rdpResult,
                                               const RenderTarget& target,
                                               const ScreenSize screen) {
    DrawBatches drawBatches = makeDrawBatches(rspResult, rdpResult, screen);
    std::vector<base::Diagnostic> diagnostics = std::move(drawBatches.diagnostics);
    if (std::expected<void, base::Diagnostic> uploaded =
            upload(drawBatches.vertices, rdpResult.textureMemories);
        !uploaded) {
        diagnostics.push_back(std::move(uploaded.error()));
        return diagnostics;
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device->getHandle());
    if (commands == nullptr) {
        diagnostics.push_back(makeSdlError("no command buffer for the frame"));
        return diagnostics;
    }
    SDL_GPURenderPass* pass = beginFrame(commands, target);
    const std::array<SDL_GPUBuffer*, 1> storage{textureMemoryBuffer.buffer.get()};
    const SDL_GPUBufferBinding vertexBinding{.buffer = vertexBuffer.buffer.get(), .offset = 0};
    for (const DrawBatch& batch : drawBatches.batches) {
        SDL_GPUGraphicsPipeline* pipeline = pipelineCache->findPipeline(batch.pipeline);
        if (pipeline == nullptr) {
            diagnostics.push_back(makeSdlError("a pipeline could not be created"));
            continue;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, storage.data(), 1);
        const SDL_Rect scissor = scaleScissor(batch.scissor, screen, target);
        SDL_SetGPUScissor(pass, &scissor);
        SDL_PushGPUVertexUniformData(commands, 0, &batch.vertexUniforms,
                                     sizeof(batch.vertexUniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &batch.fragmentUniforms,
                                       sizeof(batch.fragmentUniforms));
        SDL_DrawGPUPrimitives(pass, batch.vertexCount, 1, batch.firstVertex, 0);
    }
    SDL_EndGPURenderPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        diagnostics.push_back(makeSdlError("the frame could not be submitted"));
    }
    return diagnostics;
}

} // namespace gfx
