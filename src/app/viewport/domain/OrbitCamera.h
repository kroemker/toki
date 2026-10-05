#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

namespace toki::viewport {

/// A camera that circles a target point, in the game's world units with Y up.
/// Yaw turns about Y and is zero looking down -Z; pitch is positive above
/// the target.
class OrbitCamera {
public:
    static constexpr f32 FieldOfView = 60.0F;
    static constexpr f32 Near = 10.0F;
    static constexpr f32 Far = 12800.0F;

    [[nodiscard]] glm::vec3 getTarget() const {
        return target;
    }

    void setTarget(const glm::vec3& point) {
        target = point;
    }

    [[nodiscard]] f32 getDistance() const {
        return distance;
    }

    /// Moves to `value` from the target, within the distances zooming allows.
    void setDistance(f32 value);

    [[nodiscard]] f32 getYaw() const {
        return yaw;
    }

    [[nodiscard]] f32 getPitch() const {
        return pitch;
    }

    /// Faces the angles given, in degrees, within the pitch `rotate` allows.
    void setAngles(f32 yawDegrees, f32 pitchDegrees);

    [[nodiscard]] glm::vec3 getEye() const;

    /// Turns by angles in degrees. Pitch stops short of straight up and down.
    void rotate(f32 yawDegrees, f32 pitchDegrees);

    /// Moves the target across the view plane, by fractions of the view's
    /// height, so a drag keeps the point under the cursor.
    void pan(f32 rightFraction, f32 upFraction);

    /// Moves closer for positive steps and away for negative ones, by a fixed
    /// fraction of the distance each step.
    void zoom(f32 steps);

    [[nodiscard]] glm::mat4 makeViewMatrix() const;

    [[nodiscard]] static glm::mat4 makeProjectionMatrix(f32 aspect, f32 zFar = Far);

private:
    glm::vec3 target{0.0F};
    f32 yaw = 45.0F;
    f32 pitch = 30.0F;
    f32 distance = 2000.0F;
};

} // namespace toki::viewport
