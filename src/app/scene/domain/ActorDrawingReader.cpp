#include "ActorDrawingReader.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ItemModels.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorObjects.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

constexpr u8 DefaultSetup = 25;

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

[[nodiscard]] std::vector<ActorSegment>
findSegments(const std::span<const actor::PartSegment> segments, const std::optional<usize> object,
             ActorObjects& objects, std::vector<base::Diagnostic>& diagnostics) {
    std::vector<ActorSegment> found;
    for (const actor::PartSegment& segment : segments) {
        if (const auto* built = std::get_if<actor::BuiltList>(&segment.target)) {
            found.push_back(ActorSegment{.segment = segment.segment, .target = *built});
            continue;
        }
        if (const std::optional<u32> address =
                object ? objects.findAddress(*object, std::get<std::string>(segment.target),
                                             diagnostics)
                       : std::nullopt) {
            found.push_back(
                ActorSegment{.segment = segment.segment,
                             .target = ObjectAddress{.object = *object, .address = *address}});
        }
    }
    return found;
}

class PartReader {
public:
    PartReader(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
               ActorObjects& objects, const ItemSource& items,
               std::vector<base::Diagnostic>& diagnostics)
        : entry(entry), variant(variant), objects(objects), items(items), diagnostics(diagnostics),
          objectName(variant.object ? variant.object : entry.object) {}

    void read(ActorDrawing& drawing);

private:
    [[nodiscard]] std::optional<usize> findObject(const std::optional<std::string>& own,
                                                  const actor::ActorPart& part) {
        const std::optional<std::string>& name = own ? own : objectName;
        if (!name) {
            diagnostics.push_back(base::makeWarning(
                part.location, std::format("{}: names no object to draw from", entry.actor)));
            return std::nullopt;
        }
        return objects.findObject(*name, diagnostics);
    }

    [[nodiscard]] DrawState makeState(const actor::ActorPart& part);

    void readItem(const actor::ActorPart& part, usize index, ActorDrawing& drawing);

    [[nodiscard]] std::vector<ActorDisplayList>
    makeItemLists(const actor::ActorPart& part, usize index, const std::string& drawId);

    [[nodiscard]] std::vector<actor::ChoiceValue> listItemValues(const actor::ActorPart& part);

    [[nodiscard]] std::optional<SkeletonDrawing> readSkeleton(const actor::ActorPart& part,
                                                              usize object, u32 address);

    void swapLimbs(const actor::ActorPart& part, const object::Skeleton& skeleton,
                   SkeletonDrawing& drawing);

    [[nodiscard]] std::optional<glm::mat4> findLimbPlacement(const actor::ActorPart& part,
                                                             const ActorDrawing& drawing);

    [[nodiscard]] object::JointTable findPose(const actor::ActorPart& part,
                                              const object::Skeleton& skeleton, usize object);

    const actor::ActorEntry& entry;
    const actor::ActorVariant& variant;
    ActorObjects& objects;
    const ItemSource& items;
    std::vector<base::Diagnostic>& diagnostics;
    std::optional<std::string> objectName;
};

void PartReader::read(ActorDrawing& drawing) {
    for (const actor::PartKind kind :
         {actor::PartKind::Skeleton, actor::PartKind::DisplayList, actor::PartKind::Item}) {
        for (usize index = 0; index < variant.parts.size(); ++index) {
            const actor::ActorPart& part = variant.parts[index];
            if (part.kind != kind) {
                continue;
            }
            if (kind == actor::PartKind::Item) {
                readItem(part, index, drawing);
                continue;
            }
            const std::optional<usize> object = findObject(part.object, part);
            const std::optional<u32> address =
                object ? objects.findAddress(*object, part.symbol, diagnostics) : std::nullopt;
            if (!address) {
                continue;
            }
            if (kind == actor::PartKind::Skeleton) {
                if (std::optional<SkeletonDrawing> skeleton =
                        readSkeleton(part, *object, *address)) {
                    skeleton->part = index;
                    drawing.skeletons.push_back(std::move(*skeleton));
                }
                continue;
            }
            ActorDisplayList list{.part = index,
                                  .object = *object,
                                  .address = *address,
                                  .pass = part.pass,
                                  .state = makeState(part)};
            if (part.limb) {
                const std::optional<glm::mat4> placement = findLimbPlacement(part, drawing);
                if (!placement) {
                    continue;
                }
                list.state.placement = *placement;
            }
            drawing.displayLists.push_back(std::move(list));
        }
    }
}

void PartReader::readItem(const actor::ActorPart& part, const usize index, ActorDrawing& drawing) {
    if (items.models == nullptr) {
        return;
    }
    if (!part.itemChoice) {
        std::ranges::move(makeItemLists(part, index, part.symbol),
                          std::back_inserter(drawing.displayLists));
        return;
    }
    const auto param =
        std::ranges::find(entry.params, part.itemChoice->param, &actor::ActorParam::name);
    if (param == entry.params.end()) {
        return;
    }
    ItemLists lists{.param = *param};
    for (const actor::ChoiceValue& value : listItemValues(part)) {
        lists.byValue.emplace(value.value, makeItemLists(part, index, value.label));
    }
    drawing.items.push_back(std::move(lists));
}

std::vector<actor::ChoiceValue> PartReader::listItemValues(const actor::ActorPart& part) {
    const actor::ItemChoice& choice = *part.itemChoice;
    if (choice.table) {
        const auto table =
            std::ranges::find(items.tables, *choice.table, &actor::ValueCollection::name);
        if (table == items.tables.end()) {
            diagnostics.push_back(base::makeWarning(
                part.location, std::format("{}: picks its item from '{}', which is not a table "
                                           "of its overlay",
                                           entry.actor, *choice.table)));
            return {};
        }
        return table->values;
    }
    const auto param = std::ranges::find(entry.params, choice.param, &actor::ActorParam::name);
    std::vector<actor::ChoiceValue> values;
    for (const actor::ChoiceValue& value : param->values) {
        if (const std::optional<std::string> name =
                actor::findDrawIdName(*items.models, value.value + param->offset)) {
            values.push_back(actor::ChoiceValue{.value = value.value, .label = *name});
        }
    }
    return values;
}

std::vector<ActorDisplayList> PartReader::makeItemLists(const actor::ActorPart& part,
                                                        const usize index,
                                                        const std::string& drawId) {
    const actor::ItemModel* model = actor::findItemModel(*items.models, drawId);
    if (model == nullptr) {
        diagnostics.push_back(base::makeWarning(
            part.location,
            std::format("{}: the checkout has no get-item model '{}'", entry.actor, drawId)));
        return {};
    }
    const std::optional<usize> object = findObject(part.object ? part.object : model->object, part);
    if (!object) {
        return {};
    }
    const DrawState partState = makeState(part);
    std::vector<ActorDisplayList> lists;
    for (const actor::ItemDraw& draw : model->draws) {
        if (draw.list >= model->lists.size()) {
            diagnostics.push_back(base::makeWarning(
                part.location,
                std::format("{}: {} draws list {} of {}, which names only {}", entry.actor,
                            model->drawFunction, draw.list, drawId, model->lists.size())));
            continue;
        }
        const std::optional<u32> address =
            objects.findAddress(*object, model->lists[draw.list], diagnostics);
        if (!address) {
            continue;
        }
        DrawState state = partState;
        state.setup = part.setup.value_or(draw.setup);
        state.placement = partState.placement * makePlacement(draw.placement);
        state.isBillboard = partState.isBillboard || draw.isBillboard;
        if (!draw.tiles.empty() &&
            !std::ranges::contains(state.segments, u8{0x08}, &ActorSegment::segment)) {
            state.segments.push_back(
                ActorSegment{.segment = 0x08, .target = actor::BuiltList{.tiles = draw.tiles}});
        }
        lists.push_back(ActorDisplayList{.part = index,
                                         .object = *object,
                                         .address = *address,
                                         .pass = draw.pass,
                                         .state = std::move(state)});
    }
    return lists;
}

std::optional<glm::mat4> PartReader::findLimbPlacement(const actor::ActorPart& part,
                                                       const ActorDrawing& drawing) {
    if (drawing.skeletons.empty()) {
        diagnostics.push_back(base::makeWarning(
            part.location, std::format("{}: '{}' is placed at limb {}, and the variant draws no "
                                       "skeleton",
                                       entry.actor, part.symbol, *part.limb)));
        return std::nullopt;
    }
    const SkeletonDrawing& skeleton = drawing.skeletons.front();
    const usize index = *part.limb - 1U;
    if (index >= skeleton.pose.matrices.size()) {
        diagnostics.push_back(base::makeWarning(
            part.location,
            std::format("{}: '{}' is placed at limb {}, and the skeleton has {} limbs", entry.actor,
                        part.symbol, *part.limb, skeleton.pose.matrices.size())));
        return std::nullopt;
    }
    return skeleton.state.placement * skeleton.pose.matrices[index] * makePlacement(part.placement);
}

DrawState PartReader::makeState(const actor::ActorPart& part) {
    DrawState state{.setup = part.setup.value_or(DefaultSetup),
                    .colors = part.colors,
                    .isHilit = part.isHilit,
                    .placement = makePlacement(part.placement),
                    .isBillboard = part.isBillboard};
    const bool hasSymbols = std::ranges::any_of(part.segments, [](const auto& segment) {
        return std::holds_alternative<std::string>(segment.target);
    });
    state.segments =
        findSegments(part.segments, hasSymbols ? findObject(part.object, part) : std::nullopt,
                     objects, diagnostics);
    return state;
}

std::optional<SkeletonDrawing> PartReader::readSkeleton(const actor::ActorPart& part,
                                                        const usize object, const u32 address) {
    const object::Skeleton skeleton =
        object::readSkeleton(*objects.getObject(object).image, address);
    std::ranges::copy(skeleton.diagnostics, std::back_inserter(diagnostics));
    if (skeleton.limbs.empty()) {
        return std::nullopt;
    }
    SkeletonDrawing drawing{.object = object,
                            .pose =
                                object::poseSkeleton(skeleton, findPose(part, skeleton, object)),
                            .pass = part.pass,
                            .state = makeState(part)};
    if (!part.limbs.empty()) {
        swapLimbs(part, skeleton, drawing);
    }
    return drawing;
}

void PartReader::swapLimbs(const actor::ActorPart& part, const object::Skeleton& skeleton,
                           SkeletonDrawing& drawing) {
    for (const actor::LimbOverride& limb : part.limbs) {
        if (limb.limb > skeleton.limbs.size()) {
            diagnostics.push_back(base::makeWarning(
                part.location,
                std::format("{}: '{}' has {} limbs, so it has no limb {}", entry.actor, part.symbol,
                            skeleton.limbs.size(), limb.limb)));
        }
    }
    std::vector<object::PosedLimb> drawn;
    for (const usize index : drawing.pose.order) {
        const u32 own = skeleton.limbs[index].displayList;
        object::PosedLimb posed{
            .limb = index, .matrix = drawing.pose.matrices[index], .displayList = own};
        const auto swapped = std::ranges::find(
            part.limbs, index + 1, [](const auto& limb) { return static_cast<usize>(limb.limb); });
        if (swapped != part.limbs.end()) {
            posed.displayList = 0;
            const std::optional<usize> object =
                swapped->displayList
                    ? findObject(swapped->object ? swapped->object : part.object, part)
                    : std::nullopt;
            const std::optional<u32> address =
                object ? objects.findAddress(*object, *swapped->displayList, diagnostics)
                       : std::nullopt;
            if (address) {
                posed.displayList = *address;
                if (*object != drawing.object) {
                    drawing.limbObjects.push_back(
                        LimbObject{.drawn = drawn.size(), .object = *object});
                }
            }
        }
        if (posed.displayList != 0 || own != 0) {
            drawn.push_back(posed);
        }
    }
    drawing.pose.drawn = std::move(drawn);
}

object::JointTable PartReader::findPose(const actor::ActorPart& part,
                                        const object::Skeleton& skeleton, const usize object) {
    if (!part.pose) {
        return object::makeRestJointTable(skeleton);
    }
    const std::optional<usize> poseObject =
        part.poseObject ? objects.findObject(*part.poseObject, diagnostics) : object;
    const std::optional<u32> address =
        poseObject ? objects.findAddress(*poseObject, *part.pose, diagnostics) : std::nullopt;
    if (!address) {
        return object::makeRestJointTable(skeleton);
    }
    const std::expected<object::Animation, base::Diagnostic> animation =
        object::readAnimation(*objects.getObject(*poseObject).image, *address);
    if (!animation) {
        diagnostics.push_back(animation.error());
    } else if (animation->jointIndices.size() != skeleton.limbs.size() + 1) {
        diagnostics.push_back(base::makeWarning(
            part.location,
            std::format("'{}' moves {} joints, '{}' has {} limbs, so it is drawn at rest",
                        *part.pose, animation->jointIndices.size(), part.symbol,
                        skeleton.limbs.size())));
    } else if (animation->frameCount > 0) {
        if (part.poseFrame >= animation->frameCount) {
            diagnostics.push_back(base::makeWarning(
                part.location, std::format("'{}' has {} frames, so it has no frame {}", *part.pose,
                                           animation->frameCount, part.poseFrame)));
        }
        return object::makeJointTable(*animation,
                                      std::min<usize>(part.poseFrame, animation->frameCount - 1));
    }
    return object::makeRestJointTable(skeleton);
}

} // namespace

AdjustmentDrawing makeAdjustmentDrawing(const actor::ActorEntry& entry,
                                        const actor::ActorAdjustment& adjustment,
                                        ActorObjects& objects,
                                        std::vector<base::Diagnostic>& diagnostics) {
    const bool hasSymbols = std::ranges::any_of(adjustment.segments, [](const auto& segment) {
        return std::holds_alternative<std::string>(segment.target);
    });
    std::optional<usize> object;
    if (hasSymbols && entry.object) {
        object = objects.findObject(*entry.object, diagnostics);
    } else if (hasSymbols) {
        diagnostics.push_back(base::makeWarning(
            adjustment.location,
            std::format("{}: an adjustment points a segment at a symbol, and the entry names "
                        "no object to find it in",
                        entry.actor)));
    }
    return AdjustmentDrawing{
        .scale = adjustment.scale,
        .heightOffset = adjustment.heightOffset,
        .rotates = adjustment.rotates,
        .colors = adjustment.colors,
        .segments = findSegments(adjustment.segments, object, objects, diagnostics),
        .parts = adjustment.parts,
    };
}

[[nodiscard]] std::vector<TakenParam> findTakes(const actor::ActorEntry& entry,
                                                const actor::ChildActor& child,
                                                const actor::ActorEntry* childEntry,
                                                std::vector<base::Diagnostic>& diagnostics) {
    std::vector<TakenParam> takes;
    for (const actor::ParamTake& take : child.takes) {
        const auto from = std::ranges::find(entry.params, take.from, &actor::ActorParam::name);
        const auto to = childEntry != nullptr ? std::ranges::find(childEntry->params, take.to,
                                                                  &actor::ActorParam::name)
                                              : std::vector<actor::ActorParam>::const_iterator();
        if (from == entry.params.end() || childEntry == nullptr || to == childEntry->params.end()) {
            diagnostics.push_back(base::makeWarning(
                entry.location, std::format("{}: its child {} takes '{}' into '{}', which its "
                                            "child's entry does not have",
                                            entry.actor, child.actor, take.from, take.to)));
            continue;
        }
        takes.push_back(TakenParam{.from = *from, .to = *to});
    }
    return takes;
}

ActorDrawing makeActorDrawing(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                              ActorObjects& objects, const FindActorId& findActorId,
                              const FindEntry& findEntry, const ItemSource& items,
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
    if (drawing.kind == actor::DrawKind::Model) {
        PartReader(entry, variant, objects, items, diagnostics).read(drawing);
    }
    for (const actor::ChildActor& child : variant.children) {
        if (const std::optional<s16> id = findActorId(child.actor)) {
            drawing.children.push_back(ChildPlacement{
                .id = *id,
                .params = child.params,
                .offset = child.offset,
                .takes = findTakes(entry, child, findEntry(child.actor), diagnostics)});
        } else {
            diagnostics.push_back(base::makeWarning(
                variant.location,
                std::format("{}: the actor table has no '{}' to spawn", entry.actor, child.actor)));
        }
    }
    return drawing;
}

} // namespace toki::scene
