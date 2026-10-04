#include "OrbitCamera.h"

#include "app/viewport/Types.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace toki::viewport {

namespace {

constexpr f32 PitchLimit = 89.0F;
constexpr f32 ZoomStep = 0.15F;
constexpr f32 MinimumDistance = 20.0F;
constexpr f32 MaximumDistance = 20000.0F;
constexpr glm::vec3 Up{0.0F, 1.0F, 0.0F};

} // namespace

glm::vec3 OrbitCamera::getEye() const {
    const f32 yawRadians = glm::radians(yaw);
    const f32 pitchRadians = glm::radians(pitch);
    const glm::vec3 offset{std::cos(pitchRadians) * std::sin(yawRadians), std::sin(pitchRadians),
                           std::cos(pitchRadians) * std::cos(yawRadians)};
    return target + (offset * distance);
}

void OrbitCamera::rotate(const f32 yawDegrees, const f32 pitchDegrees) {
    yaw = std::fmod(yaw + yawDegrees, 360.0F);
    pitch = std::clamp(pitch + pitchDegrees, -PitchLimit, PitchLimit);
}

void OrbitCamera::pan(const f32 rightFraction, const f32 upFraction) {
    const glm::vec3 forward = glm::normalize(target - getEye());
    const glm::vec3 right = glm::normalize(glm::cross(forward, Up));
    const glm::vec3 up = glm::cross(right, forward);
    const f32 viewHeight = 2.0F * distance * std::tan(glm::radians(FieldOfView) / 2.0F);
    target += ((right * rightFraction) + (up * upFraction)) * viewHeight;
}

void OrbitCamera::zoom(const f32 steps) {
    distance =
        std::clamp(distance * std::pow(1.0F - ZoomStep, steps), MinimumDistance, MaximumDistance);
}

glm::mat4 OrbitCamera::makeViewMatrix() const {
    return glm::lookAt(getEye(), target, Up);
}

glm::mat4 OrbitCamera::makeProjectionMatrix(const f32 aspect) {
    return glm::perspective(glm::radians(FieldOfView), aspect, Near, Far);
}

} // namespace toki::viewport
