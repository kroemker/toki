#include "GuessedEntry.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] std::string findEntryObject(const ActorModel& model) {
    if (!model.profileObject.empty()) {
        return model.profileObject;
    }
    if (model.skeleton) {
        return model.skeleton->skeleton.object;
    }
    return model.displayLists.empty() ? std::string()
                                      : model.displayLists.front().displayList.object;
}

[[nodiscard]] std::optional<std::string> findOwnObject(const ObjectSymbol& symbol,
                                                       const std::string& object) {
    return symbol.object == object ? std::nullopt : std::optional(symbol.object);
}

[[nodiscard]] std::vector<SegmentSymbol> listSegments(const std::vector<SegmentSetting>& settings,
                                                      const Pass pass) {
    std::vector<SegmentSymbol> segments;
    for (const SegmentSetting& setting : settings) {
        if (setting.pass == pass) {
            segments.push_back(
                SegmentSymbol{.segment = setting.segment, .name = setting.symbol.name});
        }
    }
    return segments;
}

[[nodiscard]] ActorVariant makeVariant(const ActorModel& model, const std::string& object) {
    ActorVariant variant;
    if (model.skeleton) {
        const std::string& skeletonObject = model.skeleton->skeleton.object;
        ActorPart part{.kind = PartKind::Skeleton,
                       .symbol = model.skeleton->skeleton.name,
                       .object = findOwnObject(model.skeleton->skeleton, object),
                       .segments = listSegments(model.segments, Pass::Opaque)};
        if (model.skeleton->animation) {
            part.pose = model.skeleton->animation->name;
            part.poseObject = findOwnObject(*model.skeleton->animation, skeletonObject);
        }
        variant.parts.push_back(std::move(part));
        return variant;
    }
    for (const DisplayListDraw& draw : model.displayLists) {
        variant.parts.push_back(ActorPart{
            .kind = PartKind::DisplayList,
            .symbol = draw.displayList.name,
            .object = findOwnObject(draw.displayList, object),
            .segments = listSegments(draw.segments, draw.pass),
            .pass = draw.pass,
        });
    }
    return variant;
}

} // namespace

ActorEntry makeGuessedEntry(const ActorModel& model, std::string enumName) {
    ActorEntry entry{.actor = std::move(enumName)};
    const std::string object = findEntryObject(model);
    if (!object.empty()) {
        entry.object = object;
    }
    if (model.scaleSource) {
        entry.scale = model.scale;
    }
    if (model.yOffset != 0.0F) {
        entry.heightOffset = model.yOffset;
    }
    entry.params = model.params;
    if (model.hasModel()) {
        entry.variants.push_back(makeVariant(model, object));
    }
    return entry;
}

} // namespace toki::actor
