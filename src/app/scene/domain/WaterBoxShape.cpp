#include "WaterBoxShape.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace toki::scene {

namespace {

constexpr s32 LowestCoordinate = std::numeric_limits<s16>::min();
constexpr s32 HighestCoordinate = std::numeric_limits<s16>::max();

[[nodiscard]] s16 makeCoordinate(const f32 value) {
    return static_cast<s16>(
        std::clamp(static_cast<s32>(std::lround(value)), LowestCoordinate, HighestCoordinate));
}

[[nodiscard]] bool isAlongX(const WaterBoxSide side) {
    return side == WaterBoxSide::MinX || side == WaterBoxSide::MaxX;
}

[[nodiscard]] bool isMinimum(const WaterBoxSide side) {
    return side == WaterBoxSide::MinX || side == WaterBoxSide::MinZ;
}

} // namespace

glm::vec3 findWaterBoxCenter(const WaterBox& box) {
    return {static_cast<f32>(box.xMin) + (static_cast<f32>(box.xLength) / 2.0F),
            static_cast<f32>(box.ySurface),
            static_cast<f32>(box.zMin) + (static_cast<f32>(box.zLength) / 2.0F)};
}

WaterBox makeMovedWaterBox(const WaterBox& box, const glm::vec3& center) {
    WaterBox moved = box;
    moved.xMin = makeCoordinate(center.x - (static_cast<f32>(box.xLength) / 2.0F));
    moved.ySurface = makeCoordinate(center.y);
    moved.zMin = makeCoordinate(center.z - (static_cast<f32>(box.zLength) / 2.0F));
    return moved;
}

glm::vec3 findWaterBoxSidePoint(const WaterBox& box, const WaterBoxSide side) {
    glm::vec3 point = findWaterBoxCenter(box);
    if (isAlongX(side)) {
        point.x = static_cast<f32>(box.xMin) + (isMinimum(side) ? 0.0F : box.xLength);
    } else {
        point.z = static_cast<f32>(box.zMin) + (isMinimum(side) ? 0.0F : box.zLength);
    }
    return point;
}

glm::vec3 getWaterBoxSideDirection(const WaterBoxSide side) {
    const f32 sign = isMinimum(side) ? -1.0F : 1.0F;
    return isAlongX(side) ? glm::vec3(sign, 0.0F, 0.0F) : glm::vec3(0.0F, 0.0F, sign);
}

WaterBox makeResizedWaterBox(const WaterBox& box, const WaterBoxSide side, const s32 distance) {
    WaterBox resized = box;
    s16& minimum = isAlongX(side) ? resized.xMin : resized.zMin;
    s16& length = isAlongX(side) ? resized.xLength : resized.zLength;
    const s32 low = minimum;
    const s32 high = low + length;
    if (isMinimum(side)) {
        const s32 moved = std::clamp(
            low - distance, std::max(LowestCoordinate, high - HighestCoordinate), high - 1);
        minimum = static_cast<s16>(moved);
        length = static_cast<s16>(high - moved);
    } else {
        length =
            static_cast<s16>(std::clamp(high + distance, low + 1, low + HighestCoordinate) - low);
    }
    return resized;
}

} // namespace toki::scene
