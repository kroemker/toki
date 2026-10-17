#include "OverlayVertices.h"

#include "gfx/DrawBatches.h"
#include "gfx/Overlay.h"
#include "gfx/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <span>
#include <vector>

namespace gfx {

namespace {

constexpr usize VerticesPerLine = 6;

} // namespace

std::vector<GpuOverlayVertex> makeOverlayVertices(const std::span<const OverlayLine> lines) {
    std::vector<GpuOverlayVertex> vertices;
    vertices.reserve(lines.size() * VerticesPerLine);
    for (const OverlayLine& line : lines) {
        const auto makeFrom = [&](const f32 side) {
            return GpuOverlayVertex{.position = line.from,
                                    .other = line.to,
                                    .extrusion = {side, line.width},
                                    .color = line.color};
        };
        const auto makeTo = [&](const f32 side) {
            return GpuOverlayVertex{.position = line.to,
                                    .other = line.from,
                                    .extrusion = {-side, line.width},
                                    .color = line.color};
        };
        vertices.push_back(makeFrom(-1.0F));
        vertices.push_back(makeFrom(1.0F));
        vertices.push_back(makeTo(-1.0F));
        vertices.push_back(makeTo(-1.0F));
        vertices.push_back(makeFrom(1.0F));
        vertices.push_back(makeTo(1.0F));
    }
    return vertices;
}

OverlayUniforms makeOverlayUniforms(const n64::Viewport& viewport, const ScreenSize screen,
                                    const f32 opacity) {
    return OverlayUniforms{
        .viewportScale = {viewport.scale[0], viewport.scale[1], viewport.scale[2], 0.0F},
        .viewportTranslate = {viewport.translate[0], viewport.translate[1], viewport.translate[2],
                              0.0F},
        .screen = {static_cast<f32>(screen.width), static_cast<f32>(screen.height),
                   OverlayDepthBias, opacity},
    };
}

} // namespace gfx
