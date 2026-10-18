#include "BinaryAngle.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace toki::scene {

namespace {

constexpr f64 UnitsPerTurn = 65536.0;
constexpr f64 DegreesPerTurn = 360.0;

constexpr f32 GimbalLimit = 0.99999F;

} // namespace

f32 convertToDegrees(const s16 angle) {
    return static_cast<f32>(angle * DegreesPerTurn / UnitsPerTurn);
}

s16 convertToBinaryAngle(const f32 degrees) {
    const auto units = static_cast<s64>(std::llround(degrees * UnitsPerTurn / DegreesPerTurn));
    return static_cast<s16>(static_cast<u16>(units & 0xFFFF));
}

std::array<s16, 3> rotateBinaryAngles(const std::array<s16, 3>& rotation, const usize axis,
                                      const f32 degrees) {
    if (axis == 1) {
        return {rotation[0],
                static_cast<s16>(static_cast<u16>(rotation[1]) +
                                 static_cast<u16>(convertToBinaryAngle(degrees))),
                rotation[2]};
    }
    const auto toRadians = [](const f32 angle) { return glm::radians(angle); };
    glm::vec3 turnAxis(0.0F);
    turnAxis[static_cast<glm::length_t>(axis)] = 1.0F;
    glm::mat4 matrix = glm::rotate(glm::mat4(1.0F), toRadians(degrees), turnAxis);
    matrix = glm::rotate(matrix, toRadians(convertToDegrees(rotation[1])), {0.0F, 1.0F, 0.0F});
    matrix = glm::rotate(matrix, toRadians(convertToDegrees(rotation[0])), {1.0F, 0.0F, 0.0F});
    matrix = glm::rotate(matrix, toRadians(convertToDegrees(rotation[2])), {0.0F, 0.0F, 1.0F});
    const f32 sinX = std::clamp(-matrix[2][1], -1.0F, 1.0F);
    const f32 x = std::asin(sinX);
    f32 y = 0.0F;
    f32 z = 0.0F;
    if (std::abs(sinX) < GimbalLimit) {
        y = std::atan2(matrix[2][0], matrix[2][2]);
        z = std::atan2(matrix[0][1], matrix[1][1]);
    } else {
        y = std::atan2(sinX > 0.0F ? matrix[1][0] : -matrix[1][0], matrix[0][0]);
    }
    return {convertToBinaryAngle(glm::degrees(x)), convertToBinaryAngle(glm::degrees(y)),
            convertToBinaryAngle(glm::degrees(z))};
}

} // namespace toki::scene
