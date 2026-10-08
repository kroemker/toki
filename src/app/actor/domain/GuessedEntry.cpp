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
        variant.object = findOwnObject(model.skeleton->skeleton, object);
        variant.skeleton = model.skeleton->skeleton.name;
        if (model.skeleton->animation) {
            variant.animation = model.skeleton->animation->name;
            variant.animationObject = findOwnObject(*model.skeleton->animation, skeletonObject);
        }
        variant.segments = listSegments(model.segments, Pass::Opaque);
        return variant;
    }
    for (const DisplayListDraw& draw : model.displayLists) {
        variant.displayLists.push_back(DisplayListPart{
            .name = draw.displayList.name,
            .object = findOwnObject(draw.displayList, object),
            .pass = draw.pass,
            .segments = listSegments(draw.segments, draw.pass),
        });
    }
    return variant;
}

} // namespace

ActorEntry makeGuessedEntry(const ActorModel& model, std::string enumName,
                            std::optional<GuessSource> source) {
    ActorEntry entry{.actor = std::move(enumName),
                     .status = EntryStatus::Guessed,
                     .guessedFrom = std::move(source)};
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
    if (model.hasModel()) {
        entry.variants.push_back(makeVariant(model, object));
    }
    return entry;
}

} // namespace toki::actor
