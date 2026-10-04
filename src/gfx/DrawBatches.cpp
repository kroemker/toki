#include "DrawBatches.h"

#include "base/Diagnostic.h"
#include "gfx/BlendSetup.h"
#include "gfx/ShaderUniforms.h"
#include "gfx/Types.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/TextureMemory.h"
#include "n64/Viewport.h"

#include <array>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gfx {

namespace {

constexpr u32 TextureMemoryWords = n64::TextureMemorySize / 4;
constexpr f32 FixedPointPixels = 4.0F;

// Interpolation can land a rectangle's texture coordinate a hair below a whole
// texel where the RDP's stepping is exact, flooring it into the texel before.
// A 256th of a texel keeps it on the right side without crossing the 32nds the
// filter weighs in. Temporary until rectangles step coordinates themselves.
constexpr f32 TexelBias = 1.0F / 256.0F;

struct Rectangle {
    f32 left = 0.0F;
    f32 top = 0.0F;
    f32 right = 0.0F;
    f32 bottom = 0.0F;
};

struct TextureMapping {
    std::array<f32, 2> origin{};
    std::array<f32, 2> step{};
    bool flip = false;
};

f32 getPrimitiveDepth(const n64::RdpState& state) {
    return static_cast<f32>(state.primitiveDepth & 0x7FFF) / 32.0F;
}

bool isRectangleCycle(const n64::OtherMode& otherMode) {
    const n64::CycleType cycleType = otherMode.getCycleType();
    return cycleType == n64::CycleType::Copy || cycleType == n64::CycleType::Fill;
}

PipelineKey makePipelineKey(const n64::RdpState& state, const BlendSetup& blend,
                            const bool hasDepth) {
    const n64::OtherMode& otherMode = state.otherMode;
    PipelineKey key{.blend = blend};
    if (isRectangleCycle(otherMode) || !hasDepth) {
        return key;
    }
    key.depthTest = otherMode.isDepthCompared();
    key.depthWrite = otherMode.isDepthUpdated();
    key.depthCompare = otherMode.getDepthMode() == n64::DepthMode::Decal ? DepthCompare::LessOrEqual
                                                                         : DepthCompare::Less;
    if (key.depthWrite && !key.depthTest) {
        key.depthTest = true;
        key.depthCompare = DepthCompare::Always;
    }
    return key;
}

std::array<s32, 4> makeScissor(const n64::Scissor& scissor, const ScreenSize screen) {
    if (scissor.right == 0 && scissor.bottom == 0) {
        return {0, 0, static_cast<s32>(screen.width), static_cast<s32>(screen.height)};
    }
    return {scissor.left >> 2, scissor.top >> 2, (scissor.right + 3) >> 2,
            (scissor.bottom + 3) >> 2};
}

class BatchBuilder {
public:
    BatchBuilder(const n64::RspResult& rspResult, const n64::RdpResult& rdpResult,
                 const ScreenSize screen)
        : rspResult(rspResult), rdpResult(rdpResult), screen(screen),
          blendSetups(rdpResult.states.size()) {}

    [[nodiscard]] DrawBatches build() {
        for (const n64::Draw& draw : rdpResult.draws) {
            const usize stateIndex = draw.state;
            std::visit([&](const auto& primitive) { add(primitive, stateIndex); }, draw.primitive);
        }
        return std::move(result);
    }

private:
    void add(const n64::Triangle& triangle, usize stateIndex);
    void add(const n64::TextureRectangle& rectangle, usize stateIndex);
    void add(const n64::FillRectangle& rectangle, usize stateIndex);
    void addQuad(const Rectangle& rectangle, const TextureMapping& mapping, f32 depth,
                 const PrimitiveInfo& primitive, usize stateIndex, u32 address);
    void append(std::span<const GpuVertex> vertices, const DrawBatch& batch);
    [[nodiscard]] const BlendSetup& findBlendSetup(usize stateIndex, u32 address);
    [[nodiscard]] VertexUniforms makeScreenUniforms() const;

    const n64::RspResult& rspResult;
    const n64::RdpResult& rdpResult;
    ScreenSize screen;
    std::vector<std::optional<BlendSetup>> blendSetups;
    DrawBatches result;
};

const BlendSetup& BatchBuilder::findBlendSetup(const usize stateIndex, const u32 address) {
    std::optional<BlendSetup>& cached = blendSetups[stateIndex];
    if (!cached) {
        const std::expected<BlendSetup, std::string> setup =
            makeBlendSetup(rdpResult.states[stateIndex].otherMode);
        if (setup) {
            cached = *setup;
        } else {
            result.diagnostics.push_back(base::makeWarning(
                {}, std::format("display list command at 0x{:06X}: drawn without blending: {}",
                                address, setup.error())));
            cached = BlendSetup{};
        }
    }
    return *cached;
}

VertexUniforms BatchBuilder::makeScreenUniforms() const {
    return VertexUniforms{
        .viewportScale = {1.0F, -1.0F, 1.0F, 0.0F},
        .viewportTranslate = {0.0F, 0.0F, 0.0F, -1.0F},
        .screen = {static_cast<f32>(screen.width), static_cast<f32>(screen.height), 0.0F, 0.0F},
    };
}

void BatchBuilder::add(const n64::Triangle& triangle, const usize stateIndex) {
    const n64::RdpState& state = rdpResult.states[stateIndex];
    const n64::Viewport viewport = triangle.viewport < rspResult.viewports.size()
                                       ? rspResult.viewports[triangle.viewport]
                                       : n64::makeDefaultViewport();
    const bool primitiveDepth = state.otherMode.isPrimitiveDepth();
    const DrawBatch batch{
        .vertexUniforms =
            {
                .viewportScale = {viewport.scale[0], viewport.scale[1], viewport.scale[2], 0.0F},
                .viewportTranslate = {viewport.translate[0], viewport.translate[1],
                                      viewport.translate[2],
                                      primitiveDepth ? getPrimitiveDepth(state) : -1.0F},
                .screen = {static_cast<f32>(screen.width), static_cast<f32>(screen.height), 0.0F,
                           0.0F},
            },
        .fragmentUniforms = makeFragmentUniforms(
            state, {.textured = triangle.textured, .shade = triangle.shade, .tile = triangle.tile},
            findBlendSetup(stateIndex, triangle.address),
            static_cast<u32>(state.textureMemory) * TextureMemoryWords),
        .pipeline = makePipelineKey(state, findBlendSetup(stateIndex, triangle.address),
                                    triangle.depth || primitiveDepth),
        .scissor = makeScissor(state.scissor, screen),
    };
    std::array<GpuVertex, 3> vertices{};
    for (usize i = 0; i < vertices.size(); ++i) {
        const n64::ShadedVertex& source = triangle.vertices[i];
        vertices[i] = GpuVertex{
            .position = source.position, .texture = source.texture, .color = source.color};
    }
    append(vertices, batch);
}

void BatchBuilder::add(const n64::TextureRectangle& rectangle, const usize stateIndex) {
    const n64::RdpState& state = rdpResult.states[stateIndex];
    const auto& words = rectangle.words;
    const bool inclusive = isRectangleCycle(state.otherMode);
    const f32 extra = inclusive ? 1.0F : 0.0F;
    const Rectangle area{
        .left = static_cast<f32>((words[1] >> 12) & 0x0FFF) / FixedPointPixels,
        .top = static_cast<f32>(words[1] & 0x0FFF) / FixedPointPixels,
        .right = (static_cast<f32>((words[0] >> 12) & 0x0FFF) / FixedPointPixels) + extra,
        .bottom = (static_cast<f32>(words[0] & 0x0FFF) / FixedPointPixels) + extra,
    };
    const f32 copyScale = state.otherMode.getCycleType() == n64::CycleType::Copy ? 0.25F : 1.0F;
    const TextureMapping mapping{
        .origin = {static_cast<f32>(static_cast<s16>(words[2] >> 16)) / 32.0F,
                   static_cast<f32>(static_cast<s16>(words[2])) / 32.0F},
        .step = {static_cast<f32>(static_cast<s16>(words[3] >> 16)) / 1024.0F * copyScale,
                 static_cast<f32>(static_cast<s16>(words[3])) / 1024.0F},
        .flip = (words[0] >> 24) == 0xE5,
    };
    addQuad(area, mapping, getPrimitiveDepth(state),
            {.textured = true,
             .shade = false,
             .rectangle = true,
             .tile = static_cast<u8>((words[1] >> 24) & 0x07)},
            stateIndex, rectangle.address);
}

void BatchBuilder::add(const n64::FillRectangle& rectangle, const usize stateIndex) {
    const n64::RdpState& state = rdpResult.states[stateIndex];
    if (state.colorImage != 0 && state.colorImage == state.depthImage) {
        return;
    }
    const f32 extra = isRectangleCycle(state.otherMode) ? 1.0F : 0.0F;
    const Rectangle area{
        .left = static_cast<f32>(rectangle.left) / FixedPointPixels,
        .top = static_cast<f32>(rectangle.top) / FixedPointPixels,
        .right = (static_cast<f32>(rectangle.right) / FixedPointPixels) + extra,
        .bottom = (static_cast<f32>(rectangle.bottom) / FixedPointPixels) + extra,
    };
    addQuad(area, {}, getPrimitiveDepth(state), {.rectangle = true}, stateIndex, rectangle.address);
}

void BatchBuilder::addQuad(const Rectangle& rectangle, const TextureMapping& mapping,
                           const f32 depth, const PrimitiveInfo& primitive, const usize stateIndex,
                           const u32 address) {
    const n64::RdpState& state = rdpResult.states[stateIndex];
    const auto makeVertex = [&](const f32 x, const f32 y) {
        const f32 alongX = x - rectangle.left - 0.5F;
        const f32 alongY = y - rectangle.top - 0.5F;
        return GpuVertex{
            .position = {x, y, depth, 1.0F},
            .texture = {mapping.origin[0] + ((mapping.flip ? alongY : alongX) * mapping.step[0]) +
                            TexelBias,
                        mapping.origin[1] + ((mapping.flip ? alongX : alongY) * mapping.step[1]) +
                            TexelBias},
            .color = {},
        };
    };
    const std::array<GpuVertex, 6> vertices{
        makeVertex(rectangle.left, rectangle.top),     makeVertex(rectangle.right, rectangle.top),
        makeVertex(rectangle.left, rectangle.bottom),  makeVertex(rectangle.right, rectangle.top),
        makeVertex(rectangle.right, rectangle.bottom), makeVertex(rectangle.left, rectangle.bottom),
    };
    const BlendSetup& blend = findBlendSetup(stateIndex, address);
    append(vertices,
           DrawBatch{
               .vertexUniforms = makeScreenUniforms(),
               .fragmentUniforms =
                   makeFragmentUniforms(state, primitive, blend,
                                        static_cast<u32>(state.textureMemory) * TextureMemoryWords),
               .pipeline = makePipelineKey(state, blend, state.otherMode.isPrimitiveDepth()),
               .scissor = makeScissor(state.scissor, screen),
           });
}

void BatchBuilder::append(const std::span<const GpuVertex> vertices, const DrawBatch& batch) {
    const auto first = static_cast<u32>(result.vertices.size());
    result.vertices.insert(result.vertices.end(), vertices.begin(), vertices.end());
    const auto count = static_cast<u32>(vertices.size());
    if (!result.batches.empty()) {
        DrawBatch& last = result.batches.back();
        if (last.firstVertex + last.vertexCount == first &&
            last.vertexUniforms == batch.vertexUniforms &&
            last.fragmentUniforms == batch.fragmentUniforms && last.pipeline == batch.pipeline &&
            last.scissor == batch.scissor) {
            last.vertexCount += count;
            return;
        }
    }
    DrawBatch added = batch;
    added.firstVertex = first;
    added.vertexCount = count;
    result.batches.push_back(added);
}

} // namespace

DrawBatches makeDrawBatches(const n64::RspResult& rspResult, const n64::RdpResult& rdpResult,
                            const ScreenSize screen) {
    return BatchBuilder(rspResult, rdpResult, screen).build();
}

} // namespace gfx
