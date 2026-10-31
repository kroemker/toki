#pragma once

#include "app/viewport/Types.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::viewport {

/// What a transform does to what is selected.
enum class TransformKind : u8 {
    Move,
    Rotate,
    Scale,
};

/// The axes a transform is held to, X, Y and Z of the world or of `basis`;
/// none held leaves it free. A move held to one axis slides along it, held to
/// two slides in their plane; a rotation turns about the one axis held, or Y
/// when it is free; a scale stretches along the axes held, or along all of
/// them.
struct AxisConstraint {
    std::array<bool, 3> axes{};
    /// The axes held in place of the world's, one a column each: a normal of
    /// what is transformed in the column of the key pressed twice, and two
    /// axes square to it. Nothing holds to the world's.
    std::optional<glm::mat3> basis;

    [[nodiscard]] bool isFree() const {
        return !axes[0] && !axes[1] && !axes[2];
    }

    /// Holds the transform to `axis` alone, then, with a `normal` given, to
    /// the normal, and then frees it, as X pressed again and again does in
    /// Blender.
    void constrainTo(usize axis, const std::optional<glm::vec3>& normal = std::nullopt);

    /// Holds the transform to the plane of the two axes other than `axis`,
    /// then, with a `normal` given, to the plane square to the normal, and
    /// then frees it, as Shift+X does in Blender.
    void constrainToPlane(usize axis, const std::optional<glm::vec3>& normal = std::nullopt);

    /// The axis a rotation turns about.
    [[nodiscard]] usize findRotationAxis() const;

    /// Which way `axis` points in the world.
    [[nodiscard]] glm::vec3 findDirection(usize axis) const;

    /// What `axis` is called: X, Y or Z, or, held to a normal, N for the
    /// normal and U and V for the two square to it.
    [[nodiscard]] const char* findName(usize axis) const;

    friend bool operator==(const AxisConstraint&, const AxisConstraint&) = default;
};

/// What a transform snaps to: steps of a grid, and for a move, the ground
/// below each point.
struct Snapping {
    bool isGridOn = false;
    bool isGroundOn = false;
    f32 gridStep = 10.0F;
    f32 angleStep = 15.0F;
    f32 factorStep = 0.1F;
};

/// What a transform comes to: a move's offset, a rotation's angle in
/// degrees about an axis, right-handed, or a scale's factor along each axis.
struct TransformResult {
    TransformKind kind = TransformKind::Move;
    glm::vec3 offset{0.0F};
    usize axis = 1;
    f32 degrees = 0.0F;
    glm::vec3 factor{1.0F};
    /// The axes `axis` and `factor` are of, where they are not the world's.
    std::optional<glm::mat3> basis;

    friend bool operator==(const TransformResult&, const TransformResult&) = default;
};

/// One transform of what is selected, from when it begins to when it is
/// confirmed or cancelled: what the mouse or a gizmo dragged it to, or the
/// numbers typed for it, which then take the drag's place, as in Blender.
/// It knows only points, so the same session moves placements now and
/// vertices later.
class TransformSession {
public:
    explicit TransformSession(TransformKind kind) : kind(kind) {}

    [[nodiscard]] TransformKind getKind() const {
        return kind;
    }

    void setDragged(const glm::vec3& offset) {
        draggedOffset = offset;
    }

    void setDraggedAngle(const f32 degrees) {
        draggedDegrees = degrees;
    }

    void setDraggedFactor(const glm::vec3& factor) {
        draggedFactor = factor;
    }

    /// Types `character` into the field being typed: a digit, a point, or a
    /// minus, which negates the field. False when it is none of them.
    bool type(char character);

    /// Takes the last character back from the field being typed.
    void erase();

    /// Types into the next field, as Tab does: the next axis of a move.
    void moveToNextField(const AxisConstraint& constraint);

    [[nodiscard]] bool isTyping() const;

    [[nodiscard]] TransformResult makeResult(const AxisConstraint& constraint,
                                             const Snapping& snapping) const;

    /// What the session does, as it is shown over the image while it runs:
    /// `Move  X 120  Y 0  Z -30`, the field being typed in brackets.
    [[nodiscard]] std::string describe(const AxisConstraint& constraint,
                                       const Snapping& snapping) const;

private:
    [[nodiscard]] std::vector<usize> listFieldAxes(const AxisConstraint& constraint) const;
    [[nodiscard]] f32 findTyped(usize field) const;

    TransformKind kind;
    glm::vec3 draggedOffset{0.0F};
    f32 draggedDegrees = 0.0F;
    glm::vec3 draggedFactor{1.0F};
    std::array<std::string, 3> typed;
    usize field = 0;
    bool isTyped = false;
};

/// How far a drag from `start` to `mouse`, both on an image of `size` that
/// `viewProjection` draws, moves a point at `pivot`, held to `constraint`:
/// along the axis held, in the plane of the two held, or, free, in the plane
/// through `pivot` facing the camera at `eye`.
[[nodiscard]] glm::vec3 findDragOffset(const glm::mat4& viewProjection, glm::vec2 size,
                                       glm::vec2 start, glm::vec2 mouse, const glm::vec3& pivot,
                                       const glm::vec3& eye, const AxisConstraint& constraint);

/// How far, in degrees, a drag from `start` to `mouse` around `pivot` on the
/// image turns about `axis` seen from `eye`: counterclockwise on the image
/// turns right-handed about an axis pointing at the camera.
[[nodiscard]] f32 findDragAngle(glm::vec2 pivot, glm::vec2 start, glm::vec2 mouse,
                                const glm::vec3& axis, const glm::vec3& eye,
                                const glm::vec3& worldPivot);

/// How far above a point the ground below it is looked for, so a point moved
/// a little into a slope or a step still lands on top of it.
inline constexpr f32 GroundReach = 100.0F;

/// How much a drag from `start` to `mouse` stretches what is around `pivot`,
/// all three on the image: how much farther from it the mouse is.
[[nodiscard]] f32 findDragFactor(glm::vec2 pivot, glm::vec2 start, glm::vec2 mouse);

/// `points` as `result` transforms them: moved by its offset, or turned about
/// or stretched from `pivot`. With `ground` given, three corners per triangle, each moved
/// point is set down on the ground below it, if there is any.
[[nodiscard]] std::vector<glm::vec3> makeTransformedPoints(std::span<const glm::vec3> points,
                                                           const glm::vec3& pivot,
                                                           const TransformResult& result,
                                                           std::span<const glm::vec3> ground = {});

/// The height of the highest of `triangles`, three corners each, that a ray
/// straight down from `reach` above `point` meets, if one does.
[[nodiscard]] std::optional<f32> findGroundBelow(std::span<const glm::vec3> triangles,
                                                 const glm::vec3& point, f32 reach);

} // namespace toki::viewport
