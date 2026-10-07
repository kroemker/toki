#include "ActorDraws.h"

#include "app/object/domain/ObjectView.h"
#include "app/object/domain/Pose.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/DisplayListWriter.h"
#include "n64/FixedMatrix.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr f32 BinaryAngleToRadians = 2.0F * std::numbers::pi_v<f32> / 65536.0F;

[[nodiscard]] f32 makeRadians(const s16 angle) {
    return static_cast<f32>(angle) * BinaryAngleToRadians;
}

constexpr u8 FirstActorSegment = 0x08;
constexpr u8 LastActorSegment = 0x0C;
constexpr usize CodeImage = 0;

[[nodiscard]] std::vector<viewport::SegmentPointer>
makeSegmentPointers(const std::span<const ActorSegment> segments, const actor::Pass pass,
                    const usize firstImage, const std::optional<u32> emptyDisplayList) {
    std::vector<viewport::SegmentPointer> pointers;
    for (u8 segment = FirstActorSegment; segment <= LastActorSegment; ++segment) {
        const auto set = std::ranges::find_if(segments, [&](const ActorSegment& setting) {
            return setting.segment == segment && setting.pass == pass;
        });
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

class ActorWriter {
public:
    ActorWriter(const ActorDraws& draws, const usize firstImage)
        : imageIndex(firstImage + draws.images.size()), firstImage(firstImage) {}

    void writeSkeleton(const ActorDrawing& drawing, const glm::mat4& actorMatrix,
                       const ActorSetup& setup, ActorDraws& draws);

    void writeDisplayLists(const ActorDrawing& drawing, const glm::mat4& actorMatrix,
                           const ActorSetup& setup, ActorDraws& draws);

    [[nodiscard]] z64::MemoryImage makeImage() const {
        z64::MemoryImage image;
        image.segments.push_back(
            z64::ImageSegment{.number = object::LimbMatrixSegment, .bytes = writer.getBytes()});
        return image;
    }

private:
    [[nodiscard]] u32 startList(const std::optional<u32> setup) {
        const u32 start = writer.getAddress();
        if (setup) {
            writer.callDisplayList(*setup);
        }
        return start;
    }

    void endList(const u32 identity) {
        writer.loadMatrix(identity, n64::MatrixLoad::ModelView);
        writer.endDisplayList();
    }

    n64::DisplayListWriter writer{object::LimbMatrixSegment};
    usize imageIndex = 0;
    usize firstImage = 0;
};

void ActorWriter::writeSkeleton(const ActorDrawing& drawing, const glm::mat4& actorMatrix,
                                const ActorSetup& setup, ActorDraws& draws) {
    std::vector<u32> matrices;
    matrices.reserve(drawing.limbs.size());
    for (const object::PosedLimb& limb : drawing.limbs) {
        matrices.push_back(writer.addMatrix(viewport::makeFixedMatrix(actorMatrix * limb.matrix)));
    }
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());
    const u32 start = startList(setup.setupDisplayList);
    for (usize i = 0; i < drawing.limbs.size(); ++i) {
        writer.loadMatrix(matrices[i], n64::MatrixLoad::ModelView);
        writer.callDisplayList(drawing.limbs[i].displayList);
    }
    endList(identity);
    draws.opaque.push_back(viewport::ViewportDraw{
        .image = imageIndex,
        .displayList = start,
        .alsoImages = {firstImage + drawing.skeletonObject},
        .pointedSegments = makeSegmentPointers(drawing.segments, actor::Pass::Opaque, firstImage,
                                               setup.emptyDisplayList),
    });
}

void ActorWriter::writeDisplayLists(const ActorDrawing& drawing, const glm::mat4& actorMatrix,
                                    const ActorSetup& setup, ActorDraws& draws) {
    const u32 matrix = writer.addMatrix(viewport::makeFixedMatrix(actorMatrix));
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());
    for (const ActorDisplayList& list : drawing.displayLists) {
        const u32 start = startList(setup.setupDisplayList);
        writer.loadMatrix(matrix, n64::MatrixLoad::ModelView);
        writer.callDisplayList(list.address);
        endList(identity);
        std::vector<viewport::ViewportDraw>& pass =
            list.pass == actor::Pass::Translucent ? draws.translucent : draws.opaque;
        pass.push_back(viewport::ViewportDraw{
            .image = imageIndex,
            .displayList = start,
            .alsoImages = {firstImage + list.object},
            .pointedSegments =
                makeSegmentPointers(list.segments, list.pass, firstImage, setup.emptyDisplayList),
        });
    }
}

} // namespace

glm::mat4 makeActorMatrix(const PlacedActor& actor, const f32 scale, const f32 yOffset) {
    const glm::vec3 position(static_cast<f32>(actor.position[0]),
                             static_cast<f32>(actor.position[1]) + (yOffset * scale),
                             static_cast<f32>(actor.position[2]));
    glm::mat4 matrix = glm::translate(glm::mat4(1.0F), position);
    matrix = glm::rotate(matrix, makeRadians(actor.rotation[1]), glm::vec3(0.0F, 1.0F, 0.0F));
    matrix = glm::rotate(matrix, makeRadians(actor.rotation[0]), glm::vec3(1.0F, 0.0F, 0.0F));
    matrix = glm::rotate(matrix, makeRadians(actor.rotation[2]), glm::vec3(0.0F, 0.0F, 1.0F));
    return glm::scale(matrix, glm::vec3(scale));
}

ActorDraws makeActorDraws(const SceneActors& actors, const std::span<const PlacedActor> placed,
                          const usize firstImage, const ActorSetup& setup) {
    ActorDraws draws;
    for (const ActorObject& object : actors.objects) {
        draws.images.push_back(object.image);
    }
    for (usize index = 0; index < placed.size(); ++index) {
        const PlacedActor& actor = placed[index];
        const auto found = actors.models.find(actor.id);
        if (found == actors.models.end() || !found->second.drawing) {
            draws.undrawn.push_back(index);
            continue;
        }
        const SceneActorModel& model = found->second;
        const glm::mat4 matrix = makeActorMatrix(actor, model.model.scale, model.model.yOffset);
        ActorWriter writer(draws, firstImage);
        if (model.model.skeleton) {
            writer.writeSkeleton(*model.drawing, matrix, setup, draws);
        } else {
            writer.writeDisplayLists(*model.drawing, matrix, setup, draws);
        }
        draws.images.push_back(std::make_shared<const z64::MemoryImage>(writer.makeImage()));
    }
    return draws;
}

} // namespace toki::scene
