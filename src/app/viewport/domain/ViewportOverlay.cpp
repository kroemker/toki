#include "ViewportOverlay.h"

#include "app/viewport/Types.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <optional>

namespace toki::viewport {

namespace {

constexpr f32 NearestDepth = 1.0F;

[[nodiscard]] glm::vec2 makeScreenPoint(const glm::vec4& clip, const glm::vec2 size) {
    const glm::vec2 device{clip.x / clip.w, clip.y / clip.w};
    return glm::vec2{(device.x * 0.5F + 0.5F) * size.x, (0.5F - device.y * 0.5F) * size.y};
}

} // namespace

std::optional<ScreenSegment> projectSegment(const glm::mat4& viewProjection, const glm::vec3& from,
                                            const glm::vec3& to, const glm::vec2 size) {
    glm::vec4 start = viewProjection * glm::vec4(from, 1.0F);
    glm::vec4 end = viewProjection * glm::vec4(to, 1.0F);
    if (start.w < NearestDepth && end.w < NearestDepth) {
        return std::nullopt;
    }
    ScreenSegment segment;
    if (start.w < NearestDepth) {
        start += (end - start) * ((NearestDepth - start.w) / (end.w - start.w));
        segment.isFromWhole = false;
    } else if (end.w < NearestDepth) {
        end += (start - end) * ((NearestDepth - end.w) / (start.w - end.w));
        segment.isToWhole = false;
    }
    segment.from = makeScreenPoint(start, size);
    segment.to = makeScreenPoint(end, size);
    return segment;
}

} // namespace toki::viewport
