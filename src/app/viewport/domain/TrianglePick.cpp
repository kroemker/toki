#include "TrianglePick.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <variant>

namespace toki::viewport {

namespace {

/// How far under `point` the triangle `vertices` is, as its normalized depth,
/// if `point` falls in it.
[[nodiscard]] std::optional<f32> findDepthAt(const std::array<glm::vec3, 3>& vertices,
                                             const glm::vec2 point) {
    const glm::vec2 a{vertices[0]};
    const glm::vec2 b{vertices[1]};
    const glm::vec2 c{vertices[2]};
    const f32 area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area == 0.0F) {
        return std::nullopt;
    }
    const f32 first =
        ((b.x - point.x) * (c.y - point.y) - (c.x - point.x) * (b.y - point.y)) / area;
    const f32 second =
        ((c.x - point.x) * (a.y - point.y) - (a.x - point.x) * (c.y - point.y)) / area;
    const f32 third = 1.0F - first - second;
    if (first < 0.0F || second < 0.0F || third < 0.0F) {
        return std::nullopt;
    }
    return first * vertices[0].z + second * vertices[1].z + third * vertices[2].z;
}

} // namespace

std::optional<usize> findPickedTriangle(const n64::RspResult& result, const glm::vec2 point) {
    std::optional<usize> picked;
    f32 nearest = 0.0F;
    for (usize index = 0; index < result.outputs.size(); ++index) {
        const auto* triangle = std::get_if<n64::Triangle>(&result.outputs[index]);
        if (triangle == nullptr) {
            continue;
        }
        std::array<glm::vec3, 3> vertices{};
        bool isBehind = false;
        for (usize i = 0; i < vertices.size(); ++i) {
            const std::array<f32, 4>& position = triangle->vertices[i].position;
            isBehind |= position[3] <= 0.0F;
            vertices[i] = glm::vec3(position[0], position[1], position[2]) / position[3];
        }
        if (isBehind) {
            continue;
        }
        const std::optional<f32> depth = findDepthAt(vertices, point);
        if (depth && *depth >= -1.0F && *depth <= 1.0F && (!picked || *depth < nearest)) {
            picked = index;
            nearest = *depth;
        }
    }
    return picked;
}

std::optional<usize> findRangeOf(const std::span<const std::optional<OutputRange>> ranges,
                                 const usize output) {
    for (usize i = 0; i < ranges.size(); ++i) {
        if (ranges[i] && output >= ranges[i]->first && output < ranges[i]->end) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<usize> findImageAt(const std::span<const std::optional<z64::MappedImage>> images,
                                 const u32 address) {
    for (usize i = 0; i < images.size(); ++i) {
        if (images[i] && address >= images[i]->start && address < images[i]->end) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace toki::viewport
