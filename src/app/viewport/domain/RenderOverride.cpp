#include "RenderOverride.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/RenderMode.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace toki::viewport {

namespace {

constexpr u32 FilterBits = 0x03U << 12;
constexpr u32 AlphaCompareBits = 0x03U;
constexpr u32 CoverageTimesAlpha = 1U << 12;
constexpr u32 AlphaCoverageSelect = 1U << 13;
constexpr u32 ForceBlend = 1U << 14;
constexpr f32 ColorScale = 255.0F;

constexpr n64::BlenderCycle PassThrough{.first = n64::BlendColor::Pixel,
                                        .firstWeight = n64::BlendAlpha::Zero,
                                        .second = n64::BlendColor::Pixel,
                                        .secondWeight = n64::BlendWeight::One};

[[nodiscard]] u32 setBlenderCycle(const u32 low, const usize cycle,
                                  const n64::BlenderCycle& blender) {
    const u32 shift = cycle == 0 ? 2 : 0;
    const u32 mask = (0x03U << (28 + shift)) | (0x03U << (24 + shift)) | (0x03U << (20 + shift)) |
                     (0x03U << (16 + shift));
    return (low & ~mask) | (u32{static_cast<u8>(blender.first)} << (28 + shift)) |
           (u32{static_cast<u8>(blender.firstWeight)} << (24 + shift)) |
           (u32{static_cast<u8>(blender.second)} << (20 + shift)) |
           (u32{static_cast<u8>(blender.secondWeight)} << (16 + shift));
}

[[nodiscard]] bool isReading(const n64::BlenderCycle& cycle, const n64::BlendColor color) {
    return cycle.first == color || cycle.second == color;
}

[[nodiscard]] n64::OtherMode overrideOtherMode(n64::OtherMode otherMode, const RenderMode& mode) {
    if (mode.textureFilter == TextureFilter::Point) {
        otherMode.high &= ~FilterBits;
    }
    if (!mode.alphaCutout) {
        otherMode.low &= ~(AlphaCompareBits | CoverageTimesAlpha | AlphaCoverageSelect);
    }
    const bool dropsMemory = !mode.translucency && otherMode.isForceBlend();
    for (usize cycle = 0; cycle < 2; ++cycle) {
        const n64::BlenderCycle blender = otherMode.getBlenderCycle(cycle);
        if ((!mode.fog && isReading(blender, n64::BlendColor::Fog)) ||
            (dropsMemory && isReading(blender, n64::BlendColor::Memory))) {
            otherMode.low = setBlenderCycle(otherMode.low, cycle, PassThrough);
        }
    }
    if (dropsMemory) {
        otherMode.low &= ~ForceBlend;
    }
    return otherMode;
}

[[nodiscard]] n64::CombineMode selectInput(n64::CombineMode combine, const n64::CombinerInput input,
                                           const bool twoCycle) {
    using n64::CombinerInput;
    combine.cycles[0].color = {CombinerInput::Zero, CombinerInput::Zero, CombinerInput::Zero,
                               input};
    combine.cycles[1].color = {CombinerInput::Zero, CombinerInput::Zero, CombinerInput::Zero,
                               twoCycle ? CombinerInput::Combined : input};
    return combine;
}

[[nodiscard]] std::array<u8, 3> makeColor(const glm::vec3& unit) {
    std::array<u8, 3> color{};
    for (int axis = 0; axis < 3; ++axis) {
        color[static_cast<usize>(axis)] =
            static_cast<u8>(std::clamp(unit[axis], 0.0F, 1.0F) * ColorScale);
    }
    return color;
}

void setColor(n64::ShadedVertex& vertex, const std::array<u8, 3>& color) {
    vertex.color = {color[0], color[1], color[2], vertex.color[3]};
}

[[nodiscard]] glm::vec3 findWorldPosition(const glm::mat4& inverse,
                                          const n64::ShadedVertex& vertex) {
    const glm::vec4 world = inverse * glm::vec4(vertex.position[0], vertex.position[1],
                                                vertex.position[2], vertex.position[3]);
    return glm::vec3(world) / world.w;
}

void writeNormal(n64::Triangle& triangle, const glm::mat4& inverse) {
    const glm::vec3 a = findWorldPosition(inverse, triangle.vertices[0]);
    const glm::vec3 b = findWorldPosition(inverse, triangle.vertices[1]);
    const glm::vec3 c = findWorldPosition(inverse, triangle.vertices[2]);
    const glm::vec3 cross = glm::cross(b - a, c - a);
    const f32 length = glm::length(cross);
    const glm::vec3 normal = length > 0.0F ? cross / length : glm::vec3(0.0F, 1.0F, 0.0F);
    const std::array<u8, 3> color = makeColor((normal * 0.5F) + 0.5F);
    for (n64::ShadedVertex& vertex : triangle.vertices) {
        setColor(vertex, color);
    }
}

struct DepthRange {
    f32 nearest = std::numeric_limits<f32>::max();
    f32 farthest = 0.0F;
};

[[nodiscard]] DepthRange findDepthRange(const n64::RdpResult& rdpResult) {
    DepthRange range;
    for (const n64::Draw& draw : rdpResult.draws) {
        if (const auto* triangle = std::get_if<n64::Triangle>(&draw.primitive)) {
            for (const n64::ShadedVertex& vertex : triangle->vertices) {
                if (vertex.position[3] > 0.0F) {
                    range.nearest = std::min(range.nearest, vertex.position[3]);
                    range.farthest = std::max(range.farthest, vertex.position[3]);
                }
            }
        }
    }
    return range;
}

void writeDepth(n64::Triangle& triangle, const DepthRange& range) {
    const f32 span = std::max(range.farthest - range.nearest, 1.0F);
    for (n64::ShadedVertex& vertex : triangle.vertices) {
        const f32 nearness = 1.0F - ((vertex.position[3] - range.nearest) / span);
        setColor(vertex, makeColor(glm::vec3(nearness)));
    }
}

/// The states of a result reshaded by one input, each made once from the
/// state it replaces and the input it selects.
class ShadedStates {
public:
    explicit ShadedStates(std::vector<n64::RdpState>& states)
        : states(states), made(states.size()) {}

    [[nodiscard]] usize findState(const usize state, const n64::CombinerInput input) {
        const usize slot = input == n64::CombinerInput::Shade ? 0 : 1;
        std::optional<usize>& index = made[state][slot];
        if (!index) {
            n64::RdpState shaded = states[state];
            shaded.combineMode = selectInput(
                shaded.combineMode, input, shaded.otherMode.getCycleType() == n64::CycleType::Two);
            index = states.size();
            states.push_back(shaded);
        }
        return *index;
    }

private:
    std::vector<n64::RdpState>& states;
    std::vector<std::array<std::optional<usize>, 2>> made;
};

[[nodiscard]] n64::RdpResult reshade(n64::RdpResult rdpResult, const Shading shading,
                                     const glm::mat4& viewProjection) {
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const DepthRange range = findDepthRange(rdpResult);
    ShadedStates states(rdpResult.states);
    for (n64::Draw& draw : rdpResult.draws) {
        auto* triangle = std::get_if<n64::Triangle>(&draw.primitive);
        if (triangle == nullptr) {
            continue;
        }
        const bool usesTexture = shading == Shading::TextureOnly && triangle->textured;
        draw.state = states.findState(draw.state, usesTexture ? n64::CombinerInput::Texel0
                                                              : n64::CombinerInput::Shade);
        if (usesTexture) {
            continue;
        }
        triangle->shade = true;
        if (shading == Shading::Normals) {
            writeNormal(*triangle, inverse);
        } else if (shading == Shading::Depth) {
            writeDepth(*triangle, range);
        }
    }
    return rdpResult;
}

} // namespace

n64::RspOverrides makeRspOverrides(const RenderMode& mode) {
    return n64::RspOverrides{.lightsWhite = !mode.lighting, .ignoresCulling = !mode.culling};
}

n64::RdpResult applyRenderMode(n64::RdpResult rdpResult, const RenderMode& mode,
                               const glm::mat4& viewProjection) {
    for (n64::RdpState& state : rdpResult.states) {
        state.otherMode = overrideOtherMode(state.otherMode, mode);
    }
    if (mode.shading == Shading::N64) {
        return rdpResult;
    }
    return reshade(std::move(rdpResult), mode.shading, viewProjection);
}

} // namespace toki::viewport
