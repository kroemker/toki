#pragma once

#include "app/object/Types.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Skeleton.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <vector>

namespace toki::object {

/// One limb that draws, posed: its matrix from model space, and its display
/// list. Limbs come in the order the game draws them, which is also the order
/// of the matrices a flexible skeleton's display lists find in segment 13.
struct PosedLimb {
    usize limb = 0;
    glm::mat4 matrix{1.0F};
    u32 displayList = 0;
};

/// A posed skeleton: the limbs that draw, in drawing order, and for every
/// limb, drawing or not, in limb order, its matrix from model space and where
/// its joint ends up, for what an actor draws with a limb and for framing the
/// camera.
struct Pose {
    std::vector<PosedLimb> drawn;
    std::vector<glm::vec3> joints;
    std::vector<glm::mat4> matrices;
};

/// Poses `skeleton` the way `SkelAnime_DrawFlexOpa` and `SkelAnime_DrawOpa`
/// do: depth first from limb 0, a child before its siblings, each limb's
/// matrix its parent's translated to the joint and rotated about Z, then Y,
/// then X. The root's joint comes from the table's first entry instead of the
/// limb, as an animation moves the whole model.
[[nodiscard]] Pose poseSkeleton(const Skeleton& skeleton, const JointTable& jointTable);

/// The pose of a skeleton with nothing animating it: every joint where its
/// limb puts it, and no rotation.
[[nodiscard]] JointTable makeRestJointTable(const Skeleton& skeleton);

} // namespace toki::object
