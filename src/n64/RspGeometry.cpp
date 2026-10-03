#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "n64/FixedMatrix.h"
#include "n64/Lighting.h"
#include "n64/Opcode.h"
#include "n64/RspInterpreter.h"
#include "n64/RspResult.h"
#include "n64/Types.h"
#include "n64/Viewport.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>

namespace n64 {

namespace {

f32 computeFacing(const std::array<ShadedVertex, 3>& vertices) {
    const auto& a = vertices[0].position;
    const auto& b = vertices[1].position;
    const auto& c = vertices[2].position;
    return (a[0] * ((b[1] * c[3]) - (c[1] * b[3]))) - (b[0] * ((a[1] * c[3]) - (c[1] * a[3]))) +
           (c[0] * ((a[1] * b[3]) - (b[1] * a[3])));
}

} // namespace

void RspInterpreter::runVertex(const u32 word0, const u32 word1) {
    const u32 count = (word0 >> 12) & 0xFF;
    const u32 end = (word0 >> 1) & 0x7F;
    if (count == 0 || count > end || end > VertexBufferSize) {
        report(
            base::Severity::Warning,
            std::format("G_VTX loads {} vertices to end at {}, outside the {}-entry vertex buffer",
                        count, end, VertexBufferSize));
        return;
    }
    const std::optional<std::span<const u8>> bytes = readDma(word1, count * VertexSize, "vertices");
    if (!bytes) {
        return;
    }
    if (modelviewProjectionStale) {
        modelviewProjection = multiply(modelview, projection);
        modelviewProjectionStale = false;
    }
    const LightSetup setup = makeLightSetup();
    const usize viewportSlot = findViewportIndex();
    for (usize i = 0; i < count; ++i) {
        vertices[end - count + i] = VertexSlot{
            .vertex = shadeVertex(bytes->subspan(i * VertexSize, VertexSize), setup),
            .viewport = viewportSlot,
        };
    }
}

ShadedVertex RspInterpreter::shadeVertex(const std::span<const u8> bytes,
                                         const LightSetup& setup) const {
    const std::array<s64, 3> position{readS16(bytes, 0), readS16(bytes, 2), readS16(bytes, 4)};
    ShadedVertex vertex;
    for (usize column = 0; column < 4; ++column) {
        s64 sum = modelviewProjection.getElement(3, column);
        for (usize row = 0; row < 3; ++row) {
            sum += position[row] * modelviewProjection.getElement(row, column);
        }
        vertex.position[column] = static_cast<f32>(static_cast<f64>(sum) / FixedOne);
    }

    const std::array<u8, 4> color{bytes[12], bytes[13], bytes[14], bytes[15]};
    const bool lighting = (geometryMode & geometry::Lighting) != 0;
    const std::array<s8, 3> normal{static_cast<s8>(color[0]), static_cast<s8>(color[1]),
                                   static_cast<s8>(color[2])};
    vertex.color = color;
    if (lighting) {
        const std::array<u8, 3> lit =
            computeLighting(normal, setup.lights, setup.ambient, modelview);
        vertex.color = {lit[0], lit[1], lit[2], color[3]};
    }

    if (lighting && (geometryMode & geometry::TextureGen) != 0) {
        vertex.texture = computeTextureGen(
            TextureGenInput{
                .normal = normal,
                .lookAtX = setup.lookAtX,
                .lookAtY = setup.lookAtY,
                .scale = texture.scale,
                .linear = (geometryMode & geometry::TextureGenLinear) != 0,
            },
            modelview);
    } else {
        for (usize axis = 0; axis < 2; ++axis) {
            const s64 scaled = s64{readS16(bytes, 8 + (axis * 2))} * texture.scale[axis];
            vertex.texture[axis] =
                static_cast<f32>(static_cast<f64>(scaled) / FixedOne / TexelUnits);
        }
    }

    const f32 w = vertex.position[3];
    if ((geometryMode & geometry::Fog) != 0 && w != 0.0F) {
        const f32 fog = (std::max(vertex.position[2], 0.0F) / w * static_cast<f32>(fogMultiplier)) +
                        static_cast<f32>(fogOffset);
        vertex.color[3] = static_cast<u8>(std::clamp(fog, 0.0F, 255.0F));
    }
    return vertex;
}

RspInterpreter::LightSetup RspInterpreter::makeLightSetup() const {
    const auto readSlot = [this](const usize offset) {
        return readLight(std::span<const u8, LightSize>(lightMemory.data() + offset, LightSize));
    };
    LightSetup setup;
    for (usize i = 0; i < lightCount; ++i) {
        setup.lights.push_back(readSlot(LightBase + (i * LightSize)));
    }
    setup.ambient = readSlot(LightBase + (lightCount * LightSize)).color;
    setup.lookAtX = readSlot(0).direction;
    setup.lookAtY = readSlot(LightSize).direction;
    return setup;
}

usize RspInterpreter::findViewportIndex() {
    if (!viewportIndex) {
        viewportIndex = result.viewports.size();
        result.viewports.push_back(viewport);
    }
    return *viewportIndex;
}

std::optional<usize> RspInterpreter::findVertexIndex(const u32 index) {
    if (index >= VertexBufferSize) {
        report(base::Severity::Warning,
               std::format("vertex {} is outside the {}-entry vertex buffer", index,
                           VertexBufferSize));
        return std::nullopt;
    }
    return index;
}

void RspInterpreter::runModifyVertex(const u32 word0, const u32 word1) {
    const std::optional<usize> index = findVertexIndex((word0 >> 1) & 0x7FFF);
    if (!index) {
        return;
    }
    VertexSlot& slot = vertices[*index];
    const u32 where = (word0 >> 16) & 0xFF;
    switch (where) {
    case 0x10:
        slot.vertex.color = {static_cast<u8>(word1 >> 24), static_cast<u8>(word1 >> 16),
                             static_cast<u8>(word1 >> 8), static_cast<u8>(word1)};
        break;
    case 0x14:
        slot.vertex.texture = {static_cast<f32>(static_cast<s16>(word1 >> 16)) / TexelUnits,
                               static_cast<f32>(static_cast<s16>(word1)) / TexelUnits};
        break;
    case 0x18: {
        const Viewport& screen = getViewport(slot);
        const f32 x = static_cast<f32>(static_cast<s16>(word1 >> 16)) / 4.0F;
        const f32 y = static_cast<f32>(static_cast<s16>(word1)) / 4.0F;
        const f32 w = slot.vertex.position[3];
        if (screen.scale[0] != 0.0F && screen.scale[1] != 0.0F) {
            slot.vertex.position[0] = (x - screen.translate[0]) / screen.scale[0] * w;
            slot.vertex.position[1] = -(y - screen.translate[1]) / screen.scale[1] * w;
        }
        break;
    }
    default:
        report(base::Severity::Warning,
               std::format("G_MODIFYVTX at offset 0x{:02X} is not supported", where));
        break;
    }
}

void RspInterpreter::runBranchZ(const u32 word0, const u32 word1) {
    const std::optional<usize> index = findVertexIndex((word0 >> 1) & 0x7FF);
    if (!index) {
        return;
    }
    const VertexSlot& slot = vertices[*index];
    const f32 w = slot.vertex.position[3];
    if (w == 0.0F) {
        return;
    }
    const Viewport& screen = getViewport(slot);
    const f32 depth = (slot.vertex.position[2] / w * screen.scale[2]) + screen.translate[2];
    if (depth < static_cast<f32>(word1) / FixedOne) {
        pc = resolve(rdpHalf1) & DmaMask;
    }
}

void RspInterpreter::drawTriangle(const u32 indices) {
    const std::optional<usize> a = findVertexIndex(((indices >> 16) & 0xFF) / 2);
    const std::optional<usize> b = findVertexIndex(((indices >> 8) & 0xFF) / 2);
    const std::optional<usize> c = findVertexIndex((indices & 0xFF) / 2);
    if (!a || !b || !c) {
        return;
    }
    const u32 cull = geometryMode & (geometry::CullFront | geometry::CullBack);
    if (cull == (geometry::CullFront | geometry::CullBack)) {
        return;
    }
    Triangle triangle{
        .vertices = {vertices[*a].vertex, vertices[*b].vertex, vertices[*c].vertex},
        .viewport = vertices[*a].viewport,
        .shade = (geometryMode & geometry::Shade) != 0,
        .textured = texture.enabled,
        .depth = (geometryMode & geometry::ZBuffer) != 0,
        .tile = texture.tile,
        .levels = texture.levels,
        .address = commandAddress,
    };
    if (cull != 0) {
        const Viewport& screen = getViewport(vertices[*a]);
        const f32 mirror = (screen.scale[0] < 0.0F) != (screen.scale[1] < 0.0F) ? -1.0F : 1.0F;
        const f32 facing = computeFacing(triangle.vertices) * mirror;
        if ((cull == geometry::CullBack && facing < 0.0F) ||
            (cull == geometry::CullFront && facing > 0.0F)) {
            return;
        }
    }
    if ((geometryMode & geometry::SmoothShading) == 0) {
        for (ShadedVertex& vertex : triangle.vertices) {
            vertex.color = triangle.vertices[0].color;
        }
    }
    result.outputs.emplace_back(triangle);
}

} // namespace n64
