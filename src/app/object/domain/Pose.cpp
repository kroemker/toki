#include "Pose.h"

#include "app/object/Types.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Skeleton.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <array>
#include <numbers>
#include <utility>
#include <vector>

namespace toki::object {

namespace {

constexpr f32 BinaryAngleToRadians = 2.0F * std::numbers::pi_v<f32> / 65536.0F;

[[nodiscard]] f32 makeRadians(const s16 angle) {
    return static_cast<f32>(angle) * BinaryAngleToRadians;
}

class Poser {
public:
    Poser(const Skeleton& skeleton, const JointTable& jointTable)
        : skeleton(skeleton), jointTable(jointTable) {
        pose.joints.resize(skeleton.limbs.size(), glm::vec3(0.0F));
        pose.matrices.resize(skeleton.limbs.size(), glm::mat4(1.0F));
        visited.resize(skeleton.limbs.size(), false);
    }

    void poseLimb(usize index, const glm::mat4& parent);

    [[nodiscard]] Pose takePose() {
        return std::move(pose);
    }

private:
    [[nodiscard]] std::array<s16, 3> findRotation(const usize index) const {
        return index + 1 < jointTable.size() ? jointTable[index + 1] : std::array<s16, 3>{};
    }

    const Skeleton& skeleton;
    const JointTable& jointTable;
    Pose pose;
    std::vector<bool> visited;
};

void Poser::poseLimb(const usize index, const glm::mat4& parent) {
    if (index >= skeleton.limbs.size() || visited[index]) {
        return;
    }
    visited[index] = true;
    const Limb& limb = skeleton.limbs[index];
    std::array<s16, 3> position = limb.jointPosition;
    if (index == 0 && !jointTable.empty()) {
        position = jointTable[0];
    }
    const std::array<s16, 3> rotation = findRotation(index);

    glm::mat4 matrix = glm::translate(parent, glm::vec3(position[0], position[1], position[2]));
    matrix = glm::rotate(matrix, makeRadians(rotation[2]), glm::vec3(0.0F, 0.0F, 1.0F));
    matrix = glm::rotate(matrix, makeRadians(rotation[1]), glm::vec3(0.0F, 1.0F, 0.0F));
    matrix = glm::rotate(matrix, makeRadians(rotation[0]), glm::vec3(1.0F, 0.0F, 0.0F));

    pose.joints[index] = glm::vec3(matrix * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
    pose.matrices[index] = matrix;
    if (limb.displayList != 0) {
        pose.drawn.push_back(
            PosedLimb{.limb = index, .matrix = matrix, .displayList = limb.displayList});
    }
    if (limb.child != NoLimb) {
        poseLimb(limb.child, matrix);
    }
    if (limb.sibling != NoLimb) {
        poseLimb(limb.sibling, parent);
    }
}

} // namespace

Pose poseSkeleton(const Skeleton& skeleton, const JointTable& jointTable) {
    Poser poser(skeleton, jointTable);
    poser.poseLimb(0, glm::mat4(1.0F));
    return poser.takePose();
}

JointTable makeRestJointTable(const Skeleton& skeleton) {
    JointTable table(skeleton.limbs.size() + 1, std::array<s16, 3>{});
    if (!skeleton.limbs.empty()) {
        table[0] = skeleton.limbs.front().jointPosition;
    }
    return table;
}

} // namespace toki::object
