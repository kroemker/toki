#include "ActorDrawingReader.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/DrawSteps.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorObjects.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "hackeroot/ObjectFiles.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] glm::mat4 makePlacement(const actor::PartPlacement& placement) {
    const auto& [x, y, z] = placement.rotation;
    glm::mat4 matrix = glm::translate(
        glm::mat4(1.0F),
        glm::vec3(placement.translation[0], placement.translation[1], placement.translation[2]));
    matrix = glm::rotate(matrix, glm::radians(z), glm::vec3(0.0F, 0.0F, 1.0F));
    matrix = glm::rotate(matrix, glm::radians(y), glm::vec3(0.0F, 1.0F, 0.0F));
    matrix = glm::rotate(matrix, glm::radians(x), glm::vec3(1.0F, 0.0F, 0.0F));
    return glm::scale(matrix,
                      glm::vec3(placement.scale[0], placement.scale[1], placement.scale[2]));
}

void setColors(actor::DrawColors& colors, const actor::DrawColors& set) {
    if (set.primitive) {
        colors.primitive = set.primitive;
    }
    if (set.environment) {
        colors.environment = set.environment;
    }
}

void setSegment(std::vector<ActorSegment>& segments, const ActorSegment& segment) {
    std::erase_if(segments,
                  [&segment](const ActorSegment& set) { return set.segment == segment.segment; });
    segments.push_back(segment);
}

class StepRunner {
public:
    StepRunner(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
               ActorObjects& objects, std::vector<base::Diagnostic>& diagnostics)
        : entry(entry), variant(variant), objects(objects), diagnostics(diagnostics),
          objectName(variant.object ? variant.object : entry.object) {}

    void run(const std::vector<actor::DrawStep>& steps, ActorDrawing& drawing);

private:
    [[nodiscard]] std::optional<usize> findObject(const std::optional<std::string>& own,
                                                  const actor::DrawStep& step) {
        const std::optional<std::string>& name = own ? own : objectName;
        if (!name) {
            diagnostics.push_back(base::makeWarning(
                step.location, std::format("{}: names no object to draw from", entry.actor)));
            return std::nullopt;
        }
        return objects.findObject(*name, diagnostics);
    }

    [[nodiscard]] bool applyState(DrawState& state, const actor::DrawStep& step);

    [[nodiscard]] std::optional<ActorDisplayList>
    readDisplayList(const actor::DisplayListStep& list, const actor::DrawStep& step,
                    const DrawState& state);

    [[nodiscard]] std::optional<SkeletonDrawing> readSkeleton(const actor::SkeletonStep& skeleton,
                                                              const actor::DrawStep& step,
                                                              const DrawState& state);

    void poseSkeleton(const actor::SkeletonStep& skeleton, const object::Skeleton& read,
                      std::optional<usize> object, SkeletonDrawing& drawing);

    const actor::ActorEntry& entry;
    const actor::ActorVariant& variant;
    ActorObjects& objects;
    std::vector<base::Diagnostic>& diagnostics;
    std::optional<std::string> objectName;
};

bool StepRunner::applyState(DrawState& state, const actor::DrawStep& step) {
    if (const auto* setup = std::get_if<actor::SetupStep>(&step.step)) {
        state.setup = setup->index;
    } else if (const auto* colors = std::get_if<actor::ColorsStep>(&step.step)) {
        setColors(state.colors, colors->colors);
    } else if (std::holds_alternative<actor::HiliteStep>(step.step)) {
        state.isHilit = true;
    } else if (const auto* segment = std::get_if<actor::SegmentStep>(&step.step)) {
        if (segment->symbol == actor::EmptySegment) {
            std::erase_if(state.segments, [segment](const ActorSegment& set) {
                return set.segment == segment->segment;
            });
            return true;
        }
        const std::optional<usize> object = findObject(segment->object, step);
        const std::optional<u32> address =
            object ? objects.findAddress(*object, segment->symbol, diagnostics) : std::nullopt;
        if (address) {
            setSegment(state.segments, ActorSegment{
                                           .segment = segment->segment,
                                           .object = *object,
                                           .address = *address,
                                       });
        }
    } else if (const auto* transform = std::get_if<actor::TransformStep>(&step.step)) {
        if (transform->resets) {
            state.placement = glm::mat4(1.0F);
            state.isBillboard = false;
        }
        state.placement = state.placement * makePlacement(transform->placement);
        state.isBillboard = state.isBillboard || transform->isBillboard;
    } else {
        return false;
    }
    return true;
}

void StepRunner::run(const std::vector<actor::DrawStep>& steps, ActorDrawing& drawing) {
    DrawState state;
    for (const actor::DrawStep& step : steps) {
        if (applyState(state, step)) {
            continue;
        }
        if (const auto* list = std::get_if<actor::DisplayListStep>(&step.step)) {
            if (std::optional<ActorDisplayList> read = readDisplayList(*list, step, state)) {
                drawing.displayLists.push_back(std::move(*read));
            }
        } else if (const auto* skeleton = std::get_if<actor::SkeletonStep>(&step.step)) {
            drawing.skeleton = readSkeleton(*skeleton, step, state);
        }
    }
}

std::optional<ActorDisplayList> StepRunner::readDisplayList(const actor::DisplayListStep& list,
                                                            const actor::DrawStep& step,
                                                            const DrawState& state) {
    const std::optional<usize> object = findObject(list.object, step);
    const std::optional<u32> address =
        object ? objects.findAddress(*object, list.name, diagnostics) : std::nullopt;
    if (!address) {
        return std::nullopt;
    }
    return ActorDisplayList{
        .object = *object, .address = *address, .pass = list.pass, .state = state};
}

std::optional<SkeletonDrawing> StepRunner::readSkeleton(const actor::SkeletonStep& skeleton,
                                                        const actor::DrawStep& step,
                                                        const DrawState& state) {
    const std::optional<usize> object = findObject(skeleton.object, step);
    const std::optional<u32> address =
        object ? objects.findAddress(*object, skeleton.skeleton, diagnostics) : std::nullopt;
    if (!address) {
        return std::nullopt;
    }
    const object::Skeleton read = object::readSkeleton(*objects.getObject(*object).image, *address);
    std::ranges::copy(read.diagnostics, std::back_inserter(diagnostics));
    if (read.limbs.empty()) {
        return std::nullopt;
    }
    SkeletonDrawing drawing{.object = *object, .pass = skeleton.pass, .state = state};
    poseSkeleton(skeleton, read, object, drawing);
    return drawing;
}

void StepRunner::poseSkeleton(const actor::SkeletonStep& skeleton, const object::Skeleton& read,
                              const std::optional<usize> object, SkeletonDrawing& drawing) {
    object::JointTable joints = object::makeRestJointTable(read);
    if (skeleton.animation) {
        const std::optional<usize> animationObject =
            skeleton.animationObject ? objects.findObject(*skeleton.animationObject, diagnostics)
                                     : object;
        const std::optional<u32> address =
            animationObject
                ? objects.findAddress(*animationObject, *skeleton.animation, diagnostics)
                : std::nullopt;
        if (address) {
            const std::expected<object::Animation, base::Diagnostic> animation =
                object::readAnimation(*objects.getObject(*animationObject).image, *address);
            if (!animation) {
                diagnostics.push_back(animation.error());
            } else if (animation->jointIndices.size() != read.limbs.size() + 1) {
                diagnostics.push_back(base::makeWarning(
                    variant.location,
                    std::format("'{}' moves {} joints, '{}' has {} limbs, so it is drawn at rest",
                                *skeleton.animation, animation->jointIndices.size(),
                                skeleton.skeleton, read.limbs.size())));
            } else if (animation->frameCount > 0) {
                joints = object::makeJointTable(*animation, 0);
            }
        }
    }
    drawing.pose = object::poseSkeleton(read, joints);
}

} // namespace

ActorDrawing makeActorDrawing(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                              ActorObjects& objects, const FindActorId& findActorId,
                              std::vector<base::Diagnostic>& diagnostics) {
    ActorDrawing drawing{
        .kind = variant.draw.value_or(entry.draw.value_or(actor::DrawKind::Model)),
        .scale = variant.scale.value_or(entry.scale.value_or(actor::ActorModel::DefaultScale)),
        .heightOffset = variant.heightOffset.value_or(entry.heightOffset.value_or(0.0F)),
        .rotates = entry.rotates,
    };
    if (drawing.kind == actor::DrawKind::Marker) {
        return drawing;
    }
    StepRunner(entry, variant, objects, diagnostics).run(actor::makeDrawSteps(variant), drawing);
    for (const actor::ChildActor& child : variant.children) {
        if (const std::optional<s16> id = findActorId(child.actor)) {
            drawing.children.push_back(
                ChildPlacement{.id = *id, .params = child.params, .offset = child.offset});
        } else {
            diagnostics.push_back(base::makeWarning(
                variant.location,
                std::format("{}: the actor table has no '{}' to spawn", entry.actor, child.actor)));
        }
    }
    return drawing;
}

} // namespace toki::scene
