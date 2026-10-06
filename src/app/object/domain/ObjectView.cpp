#include "ObjectView.h"

#include "app/object/Types.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectAssets.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "hackeroot/GameCode.h"
#include "n64/BigEndian.h"
#include "n64/DisplayListWriter.h"
#include "n64/Opcode.h"
#include "z64/MemoryImage.h"

#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::object {

namespace {

constexpr usize CommandSize = 8;
constexpr usize VertexSize = 16;
constexpr usize MaximumCommands = 20'000;
constexpr usize MaximumDepth = 10;
constexpr u8 BranchFlag = 1;
constexpr f32 FarPerRadius = 8.0F;

constexpr viewport::ViewportLighting ObjectLighting{
    .ambient = {0x60, 0x60, 0x60},
    .lights = {viewport::ViewportLight{.direction = {0x49, 0x49, 0x49},
                                       .color = {0xC8, 0xC8, 0xC0}},
               viewport::ViewportLight{.direction = {-0x49, -0x49, -0x49},
                                       .color = {0x40, 0x40, 0x48}}},
    .fogColor = {0, 0, 0},
    .fogNear = 1000,
    .zFar = viewport::OrbitCamera::Far,
};

[[nodiscard]] Bounds makeUnion(const std::optional<Bounds>& bounds, const glm::vec3& point) {
    if (!bounds) {
        return Bounds{.lowest = point, .highest = point};
    }
    return Bounds{.lowest = glm::min(bounds->lowest, point),
                  .highest = glm::max(bounds->highest, point)};
}

class BoundsReader {
public:
    explicit BoundsReader(const z64::MemoryImage& image) : image(image) {}

    void readList(u32 address, usize depth);

    [[nodiscard]] std::optional<Bounds> getBounds() const {
        return bounds;
    }

private:
    void readVertices(u32 address, usize count);

    const z64::MemoryImage& image;
    std::optional<Bounds> bounds;
    usize commands = 0;
};

void BoundsReader::readVertices(const u32 address, const usize count) {
    const std::optional<std::span<const u8>> bytes = image.findBytes(address, count * VertexSize);
    if (!bytes) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        const usize at = i * VertexSize;
        bounds = makeUnion(bounds, glm::vec3(n64::readS16(*bytes, at), n64::readS16(*bytes, at + 2),
                                             n64::readS16(*bytes, at + 4)));
    }
}

void BoundsReader::readList(u32 address, const usize depth) {
    if (depth > MaximumDepth) {
        return;
    }
    while (commands < MaximumCommands) {
        ++commands;
        const std::optional<std::span<const u8>> bytes = image.findBytes(address, CommandSize);
        if (!bytes) {
            return;
        }
        const u32 word0 = n64::readU32(*bytes, 0);
        const u32 word1 = n64::readU32(*bytes, 4);
        const auto opcode = static_cast<n64::Opcode>(word0 >> 24);
        address += CommandSize;
        if (opcode == n64::Opcode::Vertex) {
            readVertices(word1, (word0 >> 12) & 0xFF);
        } else if (opcode == n64::Opcode::DisplayList) {
            if (((word0 >> 16) & 0xFF) == BranchFlag) {
                address = word1;
                continue;
            }
            readList(word1, depth + 1);
        } else if (opcode == n64::Opcode::EndDisplayList) {
            return;
        }
    }
}

[[nodiscard]] Bounds moveBounds(const Bounds& bounds, const glm::mat4& matrix) {
    std::optional<Bounds> moved;
    for (int corner = 0; corner < 8; ++corner) {
        glm::vec3 point = bounds.lowest;
        for (int axis = 0; axis < 3; ++axis) {
            if ((corner & (1 << axis)) != 0) {
                point[axis] = bounds.highest[axis];
            }
        }
        moved = makeUnion(moved, glm::vec3(matrix * glm::vec4(point, 1.0F)));
    }
    return moved.value_or(bounds);
}

struct PoseList {
    z64::MemoryImage image;
    u32 start = 0;
    std::optional<Bounds> bounds;
};

[[nodiscard]] PoseList writePose(const z64::MemoryImage& objectImage, const Pose& pose,
                                 const bool measuresBounds) {
    n64::DisplayListWriter writer(LimbMatrixSegment);
    std::vector<u32> matrices;
    matrices.reserve(pose.drawn.size());
    for (const PosedLimb& limb : pose.drawn) {
        matrices.push_back(writer.addMatrix(viewport::makeFixedMatrix(limb.matrix)));
    }
    PoseList list;
    list.start = writer.getAddress();
    for (usize i = 0; i < pose.drawn.size(); ++i) {
        writer.loadMatrix(matrices[i], n64::MatrixLoad::ModelView);
        writer.callDisplayList(pose.drawn[i].displayList);
        if (!measuresBounds) {
            continue;
        }
        if (const std::optional<Bounds> limbBounds =
                findDisplayListBounds(objectImage, pose.drawn[i].displayList)) {
            const Bounds moved = moveBounds(*limbBounds, pose.drawn[i].matrix);
            list.bounds = makeUnion(list.bounds, moved.lowest);
            list.bounds = makeUnion(list.bounds, moved.highest);
        }
    }
    writer.endDisplayList();
    if (measuresBounds && !list.bounds) {
        for (const glm::vec3& joint : pose.joints) {
            list.bounds = makeUnion(list.bounds, joint);
        }
    }
    list.image.segments.push_back(
        z64::ImageSegment{.number = LimbMatrixSegment, .bytes = writer.getBytes()});
    return list;
}

struct PoseFrame {
    JointTable jointTable;
    usize frameCount = 1;
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] PoseFrame findPoseFrame(const z64::MemoryImage& image, const Skeleton& skeleton,
                                      const ObjectSelection& selection) {
    PoseFrame frame;
    if (!selection.animation) {
        frame.jointTable = makeRestJointTable(skeleton);
        return frame;
    }
    const std::expected<Animation, base::Diagnostic> animation =
        readAnimation(image, *selection.animation);
    if (!animation) {
        frame.diagnostics.push_back(animation.error());
        frame.jointTable = makeRestJointTable(skeleton);
        return frame;
    }
    if (animation->jointIndices.size() != skeleton.limbs.size() + 1) {
        frame.diagnostics.push_back(base::makeWarning(
            {}, std::format("the animation moves {} joints, the skeleton has {} limbs",
                            animation->jointIndices.size(), skeleton.limbs.size())));
    }
    frame.frameCount = animation->frameCount;
    frame.jointTable = makeJointTable(*animation, selection.frame % animation->frameCount);
    return frame;
}

} // namespace

std::optional<u32> findFittingSkeleton(const Object& object, const u32 animation) {
    const z64::MemoryImage& image = *object.images[object.objectImage];
    const std::expected<Animation, base::Diagnostic> read = readAnimation(image, animation);
    if (!read) {
        return std::nullopt;
    }
    for (const ObjectAsset& asset : object.assets) {
        if (asset.kind == AssetKind::Skeleton &&
            readSkeleton(image, asset.address).limbs.size() + 1 == read->jointIndices.size()) {
            return asset.address;
        }
    }
    return std::nullopt;
}

ObjectSelection makeFirstSelection(const Object& object) {
    if (object.images.empty()) {
        return {};
    }
    for (const ObjectAsset& asset : object.assets) {
        if (asset.kind == AssetKind::Animation) {
            if (const std::optional<u32> skeleton = findFittingSkeleton(object, asset.address)) {
                return ObjectSelection{.skeleton = skeleton, .animation = asset.address};
            }
        }
    }
    const auto skeleton = std::ranges::find(object.assets, AssetKind::Skeleton, &ObjectAsset::kind);
    if (skeleton != object.assets.end()) {
        return ObjectSelection{.skeleton = skeleton->address};
    }
    const auto list = std::ranges::find(object.assets, AssetKind::DisplayList, &ObjectAsset::kind);
    if (list != object.assets.end()) {
        return ObjectSelection{.displayList = list->address};
    }
    return {};
}

std::optional<Bounds> findDisplayListBounds(const z64::MemoryImage& image, const u32 address) {
    BoundsReader reader(image);
    reader.readList(address, 0);
    return reader.getBounds();
}

ObjectContent makeObjectContent(const Object& object, const ObjectSelection& selection,
                                const std::optional<Bounds>& knownBounds) {
    ObjectContent made;
    if (object.images.empty()) {
        return made;
    }
    const z64::MemoryImage& objectImage = *object.images[object.objectImage];
    made.content.images = object.images;

    Pose pose;
    if (selection.skeleton) {
        Skeleton skeleton = readSkeleton(objectImage, *selection.skeleton);
        std::ranges::move(skeleton.diagnostics, std::back_inserter(made.diagnostics));
        PoseFrame frame = findPoseFrame(objectImage, skeleton, selection);
        std::ranges::move(frame.diagnostics, std::back_inserter(made.diagnostics));
        made.frameCount = frame.frameCount;
        pose = poseSkeleton(skeleton, frame.jointTable);
    } else if (selection.displayList) {
        pose.drawn.push_back(PosedLimb{.displayList = *selection.displayList});
    } else {
        return made;
    }

    PoseList list = writePose(objectImage, pose, !knownBounds);
    made.bounds = knownBounds ? knownBounds : list.bounds;
    const hackeroot::GameSetup setup = hackeroot::findGameSetup(*object.images.front());
    if (setup.drawConfig) {
        made.content.draws.push_back(
            viewport::ViewportDraw{.image = 0, .displayList = *setup.drawConfig});
    }
    if (setup.setupDisplayList) {
        made.content.draws.push_back(
            viewport::ViewportDraw{.image = 0, .displayList = *setup.setupDisplayList});
    }
    made.content.draws.push_back(
        viewport::ViewportDraw{.image = made.content.images.size(), .displayList = list.start});
    made.content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(list.image)));

    viewport::ViewportLighting lighting = ObjectLighting;
    if (made.bounds) {
        const f32 radius = glm::length(made.bounds->highest - made.bounds->lowest) / 2.0F;
        lighting.zFar = std::max(lighting.zFar, radius * FarPerRadius);
    }
    made.content.lighting = lighting;
    return made;
}

} // namespace toki::object
