#include "OverlayVertices.h"

#include "gfx/DrawBatches.h"
#include "gfx/Overlay.h"
#include "gfx/OverlayPipeline.h"
#include "gfx/Types.h"
#include "n64/Viewport.h"

#include <algorithm>
#include <array>
#include <vector>

namespace gfx {

namespace {

constexpr usize VerticesPerLine = 6;

} // namespace

std::vector<GpuOverlayVertex> makeOverlayVertices(const Overlay& overlay) {
    std::vector<GpuOverlayVertex> vertices;
    vertices.reserve((overlay.triangles.size() * 3) + (overlay.lines.size() * VerticesPerLine));
    for (const OverlayTriangle& triangle : overlay.triangles) {
        for (const std::array<f32, 4>& corner : triangle.corners) {
            vertices.push_back(GpuOverlayVertex{.position = corner,
                                                .other = corner,
                                                .extrusion = {},
                                                .color = triangle.color,
                                                .hiddenOpacity = triangle.hiddenOpacity});
        }
    }
    for (const OverlayLine& line : overlay.lines) {
        const auto makeFrom = [&](const f32 side) {
            return GpuOverlayVertex{.position = line.from,
                                    .other = line.to,
                                    .extrusion = {side, line.width},
                                    .color = line.color,
                                    .hiddenOpacity = line.hiddenOpacity};
        };
        const auto makeTo = [&](const f32 side) {
            return GpuOverlayVertex{.position = line.to,
                                    .other = line.from,
                                    .extrusion = {-side, line.width},
                                    .color = line.color,
                                    .hiddenOpacity = line.hiddenOpacity};
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
                                    const OverlayDepth depth) {
    return OverlayUniforms{
        .viewportScale = {viewport.scale[0], viewport.scale[1], viewport.scale[2], 0.0F},
        .viewportTranslate = {viewport.translate[0], viewport.translate[1], viewport.translate[2],
                              0.0F},
        .screen = {static_cast<f32>(screen.width), static_cast<f32>(screen.height),
                   OverlayDepthBias, depth == OverlayDepth::Hidden ? 1.0F : 0.0F},
    };
}

bool hasHiddenPart(const Overlay& overlay) {
    return std::ranges::any_of(
               overlay.triangles,
               [](const OverlayTriangle& triangle) { return triangle.hiddenOpacity > 0.0F; }) ||
           std::ranges::any_of(overlay.lines,
                               [](const OverlayLine& line) { return line.hiddenOpacity > 0.0F; });
}

} // namespace gfx
