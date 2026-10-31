#include "TransformSession.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::viewport {

namespace {

constexpr std::array<const char*, 3> AxisNames{"X", "Y", "Z"};
constexpr f32 Parallel = 1e-6F;

struct Ray {
    glm::vec3 origin{0.0F};
    glm::vec3 direction{0.0F};
};

[[nodiscard]] Ray makeRay(const glm::mat4& inverse, const glm::vec2 size, const glm::vec2 mouse) {
    const f32 x = mouse.x / size.x * 2.0F - 1.0F;
    const f32 y = 1.0F - mouse.y / size.y * 2.0F;
    const glm::vec4 near = inverse * glm::vec4(x, y, -1.0F, 1.0F);
    const glm::vec4 far = inverse * glm::vec4(x, y, 1.0F, 1.0F);
    const glm::vec3 origin = glm::vec3(near) / near.w;
    return {.origin = origin, .direction = glm::normalize(glm::vec3(far) / far.w - origin)};
}

[[nodiscard]] glm::vec3 makeAxis(const usize axis) {
    glm::vec3 made(0.0F);
    made[static_cast<glm::length_t>(axis)] = 1.0F;
    return made;
}

[[nodiscard]] std::optional<glm::vec3> findPlaneHit(const Ray& ray, const glm::vec3& point,
                                                    const glm::vec3& normal) {
    const f32 facing = glm::dot(ray.direction, normal);
    if (std::abs(facing) < Parallel) {
        return std::nullopt;
    }
    return ray.origin + ray.direction * (glm::dot(point - ray.origin, normal) / facing);
}

/// How far along `axis` from `point` the line through them comes nearest
/// `ray`.
[[nodiscard]] std::optional<f32> findAxisDistance(const Ray& ray, const glm::vec3& point,
                                                  const glm::vec3& axis) {
    const glm::vec3 between = point - ray.origin;
    const f32 along = glm::dot(axis, ray.direction);
    const f32 denominator = 1.0F - along * along;
    if (denominator < Parallel) {
        return std::nullopt;
    }
    return (along * glm::dot(ray.direction, between) - glm::dot(axis, between)) / denominator;
}

[[nodiscard]] f32 roundToStep(const f32 value, const f32 step) {
    return step > 0.0F ? std::round(value / step) * step : value;
}

[[nodiscard]] f32 findScreenAngle(const glm::vec2 pivot, const glm::vec2 point) {
    return std::atan2(pivot.y - point.y, point.x - pivot.x);
}

/// Axes with `normal` in column `axis` and two square to it after it, turning
/// as the world's do.
[[nodiscard]] glm::mat3 makeNormalBasis(const usize axis, const glm::vec3& normal) {
    const glm::vec3 across =
        std::abs(normal.y) < 0.9F ? glm::vec3(0.0F, 1.0F, 0.0F) : glm::vec3(1.0F, 0.0F, 0.0F);
    const glm::vec3 first = glm::normalize(glm::cross(across, normal));
    glm::mat3 basis(1.0F);
    basis[static_cast<glm::length_t>(axis)] = normal;
    basis[static_cast<glm::length_t>((axis + 1) % 3)] = first;
    basis[static_cast<glm::length_t>((axis + 2) % 3)] = glm::cross(normal, first);
    return basis;
}

} // namespace

void AxisConstraint::constrainTo(const usize axis, const std::optional<glm::vec3>& normal) {
    const bool isAlone = axes[axis] && std::ranges::count(axes, true) == 1;
    const bool wasWorld = !basis;
    basis.reset();
    axes = {};
    if (isAlone && wasWorld && normal) {
        basis = makeNormalBasis(axis, *normal);
    }
    axes[axis] = !isAlone || basis.has_value();
}

void AxisConstraint::constrainToPlane(const usize axis, const std::optional<glm::vec3>& normal) {
    const bool isPlane = !axes[axis] && std::ranges::count(axes, true) == 2;
    const bool wasWorld = !basis;
    basis.reset();
    axes = {true, true, true};
    axes[axis] = false;
    if (isPlane && wasWorld && normal) {
        basis = makeNormalBasis(axis, *normal);
    } else if (isPlane) {
        axes = {};
    }
}

usize AxisConstraint::findRotationAxis() const {
    if (std::ranges::count(axes, true) == 1) {
        return static_cast<usize>(std::ranges::find(axes, true) - axes.begin());
    }
    return 1;
}

glm::vec3 AxisConstraint::findDirection(const usize axis) const {
    return basis ? (*basis)[static_cast<glm::length_t>(axis)] : makeAxis(axis);
}

const char* AxisConstraint::findName(const usize axis) const {
    if (!basis) {
        return AxisNames[axis];
    }
    const auto held = static_cast<usize>(std::ranges::count(axes, true));
    const usize normal = held == 1
                             ? findRotationAxis()
                             : static_cast<usize>(std::ranges::find(axes, false) - axes.begin());
    if (axis == normal) {
        return "N";
    }
    return axis == (normal + 1) % 3 ? "U" : "V";
}

bool TransformSession::type(const char character) {
    std::string& text = typed[field];
    if (character == '-') {
        text = text.starts_with('-') ? text.substr(1) : "-" + text;
    } else if (character == '.') {
        if (text.contains('.')) {
            return true;
        }
        text += character;
    } else if (character >= '0' && character <= '9') {
        text += character;
    } else {
        return false;
    }
    isTyped = true;
    return true;
}

void TransformSession::erase() {
    if (!typed[field].empty()) {
        typed[field].pop_back();
    }
}

void TransformSession::moveToNextField(const AxisConstraint& constraint) {
    field = (field + 1) % listFieldAxes(constraint).size();
    isTyped = true;
}

bool TransformSession::isTyping() const {
    return isTyped;
}

std::vector<usize> TransformSession::listFieldAxes(const AxisConstraint& constraint) const {
    if (kind == TransformKind::Rotate) {
        return {constraint.findRotationAxis()};
    }
    if (kind == TransformKind::Scale) {
        return {0};
    }
    std::vector<usize> axes;
    for (usize axis = 0; axis < 3; ++axis) {
        if (constraint.isFree() || constraint.axes[axis]) {
            axes.push_back(axis);
        }
    }
    return axes;
}

f32 TransformSession::findTyped(const usize index) const {
    const std::string& text = typed[index];
    f32 value = 0.0F;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

TransformResult TransformSession::makeResult(const AxisConstraint& constraint,
                                             const Snapping& snapping) const {
    TransformResult result{
        .kind = kind, .axis = constraint.findRotationAxis(), .basis = constraint.basis};
    const bool isSnapped = snapping.isGridOn && !isTyped;
    if (kind == TransformKind::Rotate) {
        const f32 degrees = isTyped ? findTyped(0) : draggedDegrees;
        result.degrees = isSnapped ? roundToStep(degrees, snapping.angleStep) : degrees;
        return result;
    }
    if (kind == TransformKind::Scale) {
        for (usize axis = 0; axis < 3; ++axis) {
            if (!constraint.isFree() && !constraint.axes[axis]) {
                continue;
            }
            const auto index = static_cast<glm::length_t>(axis);
            const f32 typedFactor = typed[0].empty() ? 1.0F : findTyped(0);
            const f32 factor = isTyped ? typedFactor : draggedFactor[index];
            result.factor[index] = isSnapped ? roundToStep(factor, snapping.factorStep) : factor;
        }
        return result;
    }
    const std::vector<usize> axes = listFieldAxes(constraint);
    for (usize i = 0; i < axes.size(); ++i) {
        const glm::vec3 direction = constraint.findDirection(axes[i]);
        const f32 offset = isTyped ? findTyped(i) : glm::dot(draggedOffset, direction);
        result.offset += direction * (isSnapped ? roundToStep(offset, snapping.gridStep) : offset);
    }
    return result;
}

std::string TransformSession::describe(const AxisConstraint& constraint,
                                       const Snapping& snapping) const {
    const TransformResult result = makeResult(constraint, snapping);
    const std::vector<usize> axes = listFieldAxes(constraint);
    std::string text = kind == TransformKind::Move     ? "Move"
                       : kind == TransformKind::Rotate ? "Rotate"
                                                       : "Scale";
    for (usize i = 0; i < axes.size(); ++i) {
        f32 value = result.degrees;
        f32 precision = 10.0F;
        std::string name = constraint.findName(axes[i]);
        if (kind == TransformKind::Move) {
            value = glm::dot(result.offset, constraint.findDirection(axes[i]));
        } else if (kind == TransformKind::Scale) {
            name.clear();
            for (usize held = 0; held < 3; ++held) {
                if (constraint.isFree() || constraint.axes[held]) {
                    name += constraint.findName(held);
                    value = result.factor[static_cast<glm::length_t>(held)];
                }
            }
            precision = 1000.0F;
        }
        const std::string shown =
            !isTyped     ? std::format("{:g}", std::round(value * precision) / precision)
            : i == field ? std::format("[{}|]", typed[i])
                         : (typed[i].empty() ? "0" : typed[i]);
        text += std::format("  {} {}{}", name, shown, kind == TransformKind::Rotate ? "°" : "");
    }
    if (snapping.isGridOn) {
        text += "  · Grid";
    }
    if (snapping.isGroundOn && kind == TransformKind::Move) {
        text += "  · Ground";
    }
    return text;
}

glm::vec3 findDragOffset(const glm::mat4& viewProjection, const glm::vec2 size,
                         const glm::vec2 start, const glm::vec2 mouse, const glm::vec3& pivot,
                         const glm::vec3& eye, const AxisConstraint& constraint) {
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const Ray from = makeRay(inverse, size, start);
    const Ray to = makeRay(inverse, size, mouse);
    const auto held = static_cast<usize>(std::ranges::count(constraint.axes, true));
    if (held == 1) {
        const glm::vec3 axis = constraint.findDirection(constraint.findRotationAxis());
        const std::optional<f32> first = findAxisDistance(from, pivot, axis);
        const std::optional<f32> second = findAxisDistance(to, pivot, axis);
        return first && second ? axis * (*second - *first) : glm::vec3(0.0F);
    }
    glm::vec3 normal = glm::normalize(eye - pivot);
    if (held == 2) {
        normal = constraint.findDirection(static_cast<usize>(
            std::ranges::find(constraint.axes, false) - constraint.axes.begin()));
    }
    const std::optional<glm::vec3> first = findPlaneHit(from, pivot, normal);
    const std::optional<glm::vec3> second = findPlaneHit(to, pivot, normal);
    return first && second ? *second - *first : glm::vec3(0.0F);
}

f32 findDragAngle(const glm::vec2 pivot, const glm::vec2 start, const glm::vec2 mouse,
                  const glm::vec3& axis, const glm::vec3& eye, const glm::vec3& worldPivot) {
    f32 turned = findScreenAngle(pivot, mouse) - findScreenAngle(pivot, start);
    constexpr f32 Pi = std::numbers::pi_v<f32>;
    while (turned > Pi) {
        turned -= 2.0F * Pi;
    }
    while (turned <= -Pi) {
        turned += 2.0F * Pi;
    }
    const f32 sign = glm::dot(axis, eye - worldPivot) < 0.0F ? -1.0F : 1.0F;
    return sign * turned * 180.0F / Pi;
}

f32 findDragFactor(const glm::vec2 pivot, const glm::vec2 start, const glm::vec2 mouse) {
    constexpr f32 Closest = 1.0F;
    const f32 from = glm::length(start - pivot);
    if (from < Closest) {
        return 1.0F;
    }
    return glm::length(mouse - pivot) / from;
}

std::optional<f32> findGroundBelow(const std::span<const glm::vec3> triangles,
                                   const glm::vec3& point, const f32 reach) {
    const f32 top = point.y + reach;
    std::optional<f32> ground;
    for (usize i = 0; i + 2 < triangles.size(); i += 3) {
        const glm::vec3& a = triangles[i];
        const glm::vec3& b = triangles[i + 1];
        const glm::vec3& c = triangles[i + 2];
        const f32 area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(area) < Parallel) {
            continue;
        }
        const f32 first =
            ((b.x - point.x) * (c.z - point.z) - (c.x - point.x) * (b.z - point.z)) / area;
        const f32 second =
            ((c.x - point.x) * (a.z - point.z) - (a.x - point.x) * (c.z - point.z)) / area;
        const f32 third = 1.0F - first - second;
        if (first < 0.0F || second < 0.0F || third < 0.0F) {
            continue;
        }
        const f32 height = first * a.y + second * b.y + third * c.y;
        if (height <= top && (!ground || height > *ground)) {
            ground = height;
        }
    }
    return ground;
}

std::vector<glm::vec3> makeTransformedPoints(const std::span<const glm::vec3> points,
                                             const glm::vec3& pivot, const TransformResult& result,
                                             const std::span<const glm::vec3> ground) {
    std::vector<glm::vec3> transformed;
    const glm::mat3 basis = result.basis.value_or(glm::mat3(1.0F));
    if (result.kind == TransformKind::Rotate) {
        const glm::vec3 axis = basis[static_cast<glm::length_t>(result.axis)];
        const glm::mat4 turn = glm::rotate(glm::mat4(1.0F), glm::radians(result.degrees), axis);
        for (const glm::vec3& point : points) {
            transformed.push_back(pivot + glm::vec3(turn * glm::vec4(point - pivot, 0.0F)));
        }
        return transformed;
    }
    if (result.kind == TransformKind::Scale) {
        for (const glm::vec3& point : points) {
            transformed.push_back(
                pivot + (basis * (result.factor * (glm::transpose(basis) * (point - pivot)))));
        }
        return transformed;
    }
    for (const glm::vec3& point : points) {
        glm::vec3 moved = point + result.offset;
        if (!ground.empty()) {
            if (const std::optional<f32> height = findGroundBelow(ground, moved, GroundReach)) {
                moved.y = *height;
            }
        }
        transformed.push_back(moved);
    }
    return transformed;
}

} // namespace toki::viewport
