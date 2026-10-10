#include "ActorDraws.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/ObjectView.h"
#include "app/object/domain/Pose.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "hackeroot/GameCode.h"
#include "n64/DisplayListWriter.h"
#include "n64/FixedMatrix.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr f32 BinaryAngleToRadians = 2.0F * std::numbers::pi_v<f32> / 65536.0F;
constexpr u8 FirstActorSegment = 0x08;
constexpr u8 LastActorSegment = 0x0C;
constexpr usize CodeImage = 0;

[[nodiscard]] f32 makeRadians(const s16 angle) {
    return static_cast<f32>(angle) * BinaryAngleToRadians;
}

[[nodiscard]] std::vector<viewport::SegmentPointer>
makeSegmentPointers(const std::span<const ActorSegment> segments, const usize firstImage,
                    const std::optional<u32> emptyDisplayList) {
    std::vector<viewport::SegmentPointer> pointers;
    for (u8 segment = FirstActorSegment; segment <= LastActorSegment; ++segment) {
        const auto set = std::ranges::find(segments, segment, &ActorSegment::segment);
        if (set != segments.end()) {
            pointers.push_back(viewport::SegmentPointer{
                .segment = segment, .image = firstImage + set->object, .address = set->address});
        } else if (emptyDisplayList) {
            pointers.push_back(viewport::SegmentPointer{
                .segment = segment, .image = CodeImage, .address = *emptyDisplayList});
        }
    }
    return pointers;
}

void setColors(n64::DisplayListWriter& writer, const actor::DrawColors& colors) {
    if (colors.primitive) {
        writer.setPrimitiveColor(*colors.primitive);
    }
    if (colors.environment) {
        writer.setEnvironmentColor(*colors.environment);
    }
}

[[nodiscard]] std::optional<u32> findSetup(const ActorSetup& setup, const usize index) {
    return index < setup.setupDisplayLists.size() ? std::optional(setup.setupDisplayLists[index])
                                                  : std::nullopt;
}

class ActorWriter {
public:
    ActorWriter(ActorDraws& draws, const usize firstImage, const ActorSetup& setup,
                const glm::vec3& position)
        : draws(draws), imageIndex(firstImage + draws.images.size()), firstImage(firstImage),
          setup(setup), position(position) {}

    void writeSkeleton(const SkeletonDrawing& skeleton, const glm::mat4& actorMatrix);

    void writeDisplayList(const ActorDisplayList& list, const glm::mat4& parent);

    [[nodiscard]] z64::MemoryImage makeImage() const {
        z64::MemoryImage image;
        image.segments.push_back(
            z64::ImageSegment{.number = object::LimbMatrixSegment, .bytes = writer.getBytes()});
        return image;
    }

private:
    [[nodiscard]] u32 startList(const std::optional<u32> setupList) {
        const u32 start = writer.getAddress();
        if (setupList) {
            writer.callDisplayList(*setupList);
        }
        return start;
    }

    [[nodiscard]] std::vector<viewport::ViewportDraw>& findPass(const actor::Pass pass) {
        return pass == actor::Pass::Translucent ? draws.translucent : draws.opaque;
    }

    [[nodiscard]] std::optional<glm::vec3> findLookAtTarget(const DrawState& state) const {
        return state.isHilit ? std::optional(position) : std::nullopt;
    }

    ActorDraws& draws;
    n64::DisplayListWriter writer{object::LimbMatrixSegment};
    usize imageIndex = 0;
    usize firstImage = 0;
    const ActorSetup& setup;
    glm::vec3 position;
};

void ActorWriter::writeSkeleton(const SkeletonDrawing& skeleton, const glm::mat4& actorMatrix) {
    const object::Pose& pose = skeleton.pose;
    const glm::mat4 placed = actorMatrix * skeleton.state.placement;
    std::vector<u32> matrices;
    matrices.reserve(pose.drawn.size());
    for (const object::PosedLimb& limb : pose.drawn) {
        matrices.push_back(writer.addMatrix(viewport::makeFixedMatrix(placed * limb.matrix)));
    }
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());
    const u32 start = startList(findSetup(setup, skeleton.state.setup));
    setColors(writer, skeleton.state.colors);
    for (usize i = 0; i < pose.drawn.size(); ++i) {
        writer.loadMatrix(matrices[i], n64::MatrixLoad::ModelView);
        writer.callDisplayList(pose.drawn[i].displayList);
    }
    writer.loadMatrix(identity, n64::MatrixLoad::ModelView);
    writer.endDisplayList();
    findPass(skeleton.pass)
        .push_back(viewport::ViewportDraw{
            .image = imageIndex,
            .displayList = start,
            .alsoImages = {firstImage + skeleton.object},
            .pointedSegments =
                makeSegmentPointers(skeleton.state.segments, firstImage, setup.emptyDisplayList),
            .lookAtTarget = findLookAtTarget(skeleton.state),
        });
}

void ActorWriter::writeDisplayList(const ActorDisplayList& list, const glm::mat4& parent) {
    const u32 start = startList(findSetup(setup, list.state.setup));
    setColors(writer, list.state.colors);
    writer.callDisplayList(list.address);
    writer.endDisplayList();
    findPass(list.pass).push_back(viewport::ViewportDraw{
        .image = imageIndex,
        .displayList = start,
        .alsoImages = {firstImage + list.object},
        .pointedSegments =
            makeSegmentPointers(list.state.segments, firstImage, setup.emptyDisplayList),
        .modelView = parent * list.state.placement,
        .isBillboard = list.state.isBillboard,
        .lookAtTarget = findLookAtTarget(list.state),
    });
}

[[nodiscard]] PlacedActor placeChild(const PlacedActor& parent, const ChildPlacement& child,
                                     const ActorDrawing& drawing) {
    const glm::mat4 turned = makeActorMatrix(parent, 1.0F, 0.0F, drawing.rotates);
    const glm::vec4 at =
        turned * glm::vec4(child.offset[0], child.offset[1], child.offset[2], 1.0F);
    return PlacedActor{
        .id = child.id,
        .position = {static_cast<s16>(std::lround(at.x)), static_cast<s16>(std::lround(at.y)),
                     static_cast<s16>(std::lround(at.z))},
        .rotation = parent.rotation,
        .params = child.params,
        .age = parent.age,
    };
}

[[nodiscard]] const ActorDrawing* findDrawn(const SceneActors& actors, const PlacedActor& actor) {
    const auto found = actors.models.find(actor.id);
    if (found == actors.models.end()) {
        return nullptr;
    }
    const ActorDrawing* drawing = found->second.findDrawing(
        makePlacementValues(actors, actor.params, actor.rotation, actor.age));
    return drawing != nullptr && drawing->isDrawn() ? drawing : nullptr;
}

void writeActor(ActorDraws& draws, const PlacedActor& actor, const ActorDrawing& drawing,
                const usize firstImage, const ActorSetup& setup) {
    ActorWriter writer(draws, firstImage, setup,
                       glm::vec3(actor.position[0], actor.position[1], actor.position[2]));
    const glm::mat4 matrix =
        makeActorMatrix(actor, drawing.scale, drawing.heightOffset, drawing.rotates);
    for (const SkeletonDrawing& skeleton : drawing.skeletons) {
        writer.writeSkeleton(skeleton, matrix);
    }
    for (const ActorDisplayList& list : drawing.displayLists) {
        writer.writeDisplayList(list, matrix);
    }
    draws.images.push_back(std::make_shared<const z64::MemoryImage>(writer.makeImage()));
}

} // namespace

glm::mat4 makeActorMatrix(const PlacedActor& actor, const f32 scale, const f32 yOffset,
                          const actor::RotationAxes& rotates) {
    const glm::vec3 position(static_cast<f32>(actor.position[0]),
                             static_cast<f32>(actor.position[1]) + (yOffset * scale),
                             static_cast<f32>(actor.position[2]));
    glm::mat4 matrix = glm::translate(glm::mat4(1.0F), position);
    if (rotates.y) {
        matrix = glm::rotate(matrix, makeRadians(actor.rotation[1]), glm::vec3(0.0F, 1.0F, 0.0F));
    }
    if (rotates.x) {
        matrix = glm::rotate(matrix, makeRadians(actor.rotation[0]), glm::vec3(1.0F, 0.0F, 0.0F));
    }
    if (rotates.z) {
        matrix = glm::rotate(matrix, makeRadians(actor.rotation[2]), glm::vec3(0.0F, 0.0F, 1.0F));
    }
    return glm::scale(matrix, glm::vec3(scale));
}

ActorSetup makeActorSetup(const z64::MemoryImage& code) {
    return ActorSetup{.setupDisplayLists = hackeroot::listSetupDisplayLists(code),
                      .emptyDisplayList = code.findAddress("gEmptyDL")};
}

ActorDraws makeActorDraws(const SceneActors& actors, const std::span<const PlacedActor> placed,
                          const usize firstImage, const ActorSetup& setup) {
    ActorDraws draws;
    for (const ActorObject& object : actors.objects) {
        draws.images.push_back(object.image);
    }
    for (usize index = 0; index < placed.size(); ++index) {
        const PlacedActor& actor = placed[index];
        const ActorDrawing* drawing = findDrawn(actors, actor);
        if (drawing == nullptr) {
            draws.undrawn.push_back(index);
            continue;
        }
        if (drawing->kind != actor::DrawKind::Children) {
            writeActor(draws, actor, *drawing, firstImage, setup);
        }
        for (const ChildPlacement& child : drawing->children) {
            const PlacedActor placedChild = placeChild(actor, child, *drawing);
            if (const ActorDrawing* childDrawing = findDrawn(actors, placedChild)) {
                writeActor(draws, placedChild, *childDrawing, firstImage, setup);
            }
        }
    }
    return draws;
}

} // namespace toki::scene
