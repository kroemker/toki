#include "ActorDrawingReader.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
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
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

[[nodiscard]] actor::DrawColors mergeColors(const actor::DrawColors& own,
                                            const actor::DrawColors& inherited) {
    return actor::DrawColors{.primitive = own.primitive ? own.primitive : inherited.primitive,
                             .environment =
                                 own.environment ? own.environment : inherited.environment};
}

class DrawingReader {
public:
    DrawingReader(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                  ActorObjects& objects, std::vector<base::Diagnostic>& diagnostics)
        : entry(entry), variant(variant), objects(objects), diagnostics(diagnostics),
          objectName(variant.object ? variant.object : entry.object) {}

    [[nodiscard]] std::optional<usize> findObject(const std::optional<std::string>& name) {
        if (!name) {
            diagnostics.push_back(base::makeWarning(
                entry.location, std::format("{}: names no object to draw from", entry.actor)));
            return std::nullopt;
        }
        return objects.findObject(*name, diagnostics);
    }

    [[nodiscard]] std::vector<ActorSegment>
    readSegments(const std::vector<actor::SegmentSymbol>& symbols, std::optional<usize> object);

    [[nodiscard]] std::optional<SkeletonDrawing> readSkeleton();

    [[nodiscard]] std::vector<ActorDisplayList> readDisplayLists();

    [[nodiscard]] std::optional<ActorShadow> readShadow();

    const actor::ActorEntry& entry;
    const actor::ActorVariant& variant;
    ActorObjects& objects;
    std::vector<base::Diagnostic>& diagnostics;
    std::optional<std::string> objectName;
};

std::vector<ActorSegment>
DrawingReader::readSegments(const std::vector<actor::SegmentSymbol>& symbols,
                            const std::optional<usize> object) {
    std::vector<ActorSegment> segments;
    for (const actor::SegmentSymbol& symbol : symbols) {
        const std::optional<u32> address =
            object ? objects.findAddress(*object, symbol.name, diagnostics) : std::nullopt;
        if (address) {
            segments.push_back(
                ActorSegment{.segment = symbol.segment, .object = *object, .address = *address});
        }
    }
    return segments;
}

std::optional<SkeletonDrawing> DrawingReader::readSkeleton() {
    const std::optional<usize> object = findObject(objectName);
    const std::optional<u32> address =
        object ? objects.findAddress(*object, *variant.skeleton, diagnostics) : std::nullopt;
    if (!address) {
        return std::nullopt;
    }
    const object::Skeleton skeleton =
        object::readSkeleton(*objects.getObject(*object).image, *address);
    std::ranges::copy(skeleton.diagnostics, std::back_inserter(diagnostics));
    if (skeleton.limbs.empty()) {
        return std::nullopt;
    }
    SkeletonDrawing drawing{.object = *object, .frame = variant.frame.value_or(0)};
    object::JointTable joints = object::makeRestJointTable(skeleton);
    if (variant.animation) {
        const std::optional<usize> animationObject =
            variant.animationObject ? objects.findObject(*variant.animationObject, diagnostics)
                                    : object;
        const std::optional<u32> animationAddress =
            animationObject ? objects.findAddress(*animationObject, *variant.animation, diagnostics)
                            : std::nullopt;
        if (animationAddress) {
            const std::expected<object::Animation, base::Diagnostic> animation =
                object::readAnimation(*objects.getObject(*animationObject).image,
                                      *animationAddress);
            if (!animation) {
                diagnostics.push_back(animation.error());
            } else if (animation->jointIndices.size() != skeleton.limbs.size() + 1) {
                diagnostics.push_back(base::makeWarning(
                    variant.location,
                    std::format("'{}' moves {} joints, '{}' has {} limbs, so it is drawn at rest",
                                *variant.animation, animation->jointIndices.size(),
                                *variant.skeleton, skeleton.limbs.size())));
            } else if (animation->frameCount > 0) {
                joints = object::makeJointTable(*animation, drawing.frame % animation->frameCount);
                drawing.animation = ActorAnimation{.skeleton = skeleton, .animation = *animation};
            }
        }
    }
    drawing.limbs = object::poseSkeleton(skeleton, joints).drawn;
    return drawing;
}

std::vector<ActorDisplayList> DrawingReader::readDisplayLists() {
    std::vector<ActorDisplayList> lists;
    const std::optional<usize> variantObject =
        objectName ? objects.findObject(*objectName, diagnostics) : std::nullopt;
    for (const actor::DisplayListPart& part : variant.displayLists) {
        const std::optional<usize> object = findObject(part.object ? part.object : objectName);
        const std::optional<u32> address =
            object ? objects.findAddress(*object, part.name, diagnostics) : std::nullopt;
        if (!address) {
            continue;
        }
        std::vector<ActorSegment> segments = readSegments(variant.segments, variantObject);
        for (ActorSegment& own : readSegments(part.segments, object)) {
            std::erase_if(segments, [&own](const ActorSegment& inherited) {
                return inherited.segment == own.segment;
            });
            segments.push_back(own);
        }
        lists.push_back(ActorDisplayList{
            .object = *object,
            .address = *address,
            .pass = part.pass,
            .placement = makePlacement(part.placement),
            .colors = mergeColors(part.colors, variant.colors),
            .isBillboard = part.isBillboard.value_or(variant.isBillboard.value_or(false)),
            .segments = std::move(segments),
        });
    }
    return lists;
}

std::optional<ActorShadow> DrawingReader::readShadow() {
    if (!entry.shadow || entry.shadow->kind == actor::ShadowKind::None) {
        return std::nullopt;
    }
    const actor::ShadowKind kind = entry.shadow->kind;
    const std::optional<usize> keep =
        objects.findObject(std::string(hackeroot::KeepObjectName), diagnostics);
    const char* list = kind == actor::ShadowKind::Horse ? "gHorseShadowDL" : "gCircleShadowDL";
    const std::optional<u32> address =
        keep ? objects.findAddress(*keep, list, diagnostics) : std::nullopt;
    if (!address) {
        return std::nullopt;
    }
    return ActorShadow{
        .object = *keep,
        .address = *address,
        .color = kind == actor::ShadowKind::WhiteCircle ? std::array<u8, 4>{0xFF, 0xFF, 0xFF, 0xFF}
                                                        : std::array<u8, 4>{0, 0, 0, 0xFF},
        .size = entry.shadow->size,
        .turnsWithActor = kind == actor::ShadowKind::Horse,
    };
}

} // namespace

ActorDrawing makeActorDrawing(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                              ActorObjects& objects, const FindActorId& findActorId,
                              std::vector<base::Diagnostic>& diagnostics) {
    ActorDrawing drawing{
        .kind = variant.draw.value_or(entry.draw.value_or(actor::DrawKind::Model)),
        .colors = variant.colors,
        .scale = variant.scale.value_or(entry.scale.value_or(actor::ActorModel::DefaultScale)),
        .heightOffset = variant.heightOffset.value_or(entry.heightOffset.value_or(0.0F)),
        .rotates = entry.rotates,
    };
    if (drawing.kind == actor::DrawKind::Marker) {
        return drawing;
    }
    DrawingReader reader(entry, variant, objects, diagnostics);
    if (variant.skeleton) {
        drawing.skeleton = reader.readSkeleton();
        if (drawing.skeleton) {
            drawing.segments =
                reader.readSegments(variant.segments, std::optional(drawing.skeleton->object));
        }
    } else {
        drawing.displayLists = reader.readDisplayLists();
    }
    drawing.shadow = reader.readShadow();
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
