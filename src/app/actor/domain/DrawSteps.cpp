#include "DrawSteps.h"

#include "app/actor/domain/ActorEntry.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

class StepMaker {
public:
    explicit StepMaker(const ActorVariant& variant) : variant(variant) {}

    template <typename Step>
    void add(Step step) {
        steps.push_back(DrawStep{.step = std::move(step), .location = variant.location});
    }

    void addColors(const DrawColors& colors) {
        if (colors.primitive || colors.environment) {
            add(ColorsStep{.colors = colors});
        }
    }

    void addSegments(const std::vector<SegmentSymbol>& segments,
                     const std::optional<std::string>& object) {
        for (const SegmentSymbol& segment : segments) {
            add(SegmentStep{.segment = segment.segment, .symbol = segment.name, .object = object});
        }
    }

    [[nodiscard]] std::vector<DrawStep> takeSteps() {
        return std::move(steps);
    }

    const ActorVariant& variant;

private:
    std::vector<DrawStep> steps;
};

} // namespace

std::vector<DrawStep> makeDrawSteps(const ActorVariant& variant) {
    if (!variant.steps.empty()) {
        return variant.steps;
    }
    StepMaker maker(variant);
    if (variant.skeleton) {
        maker.addColors(variant.colors);
        maker.addSegments(variant.segments, std::nullopt);
        maker.add(SkeletonStep{.skeleton = *variant.skeleton,
                               .animation = variant.animation,
                               .animationObject = variant.animationObject,
                               .frame = variant.frame});
        return maker.takeSteps();
    }
    std::vector<u8> ownSegments;
    for (const DisplayListPart& part : variant.displayLists) {
        for (const u8 segment : ownSegments) {
            const auto isSet = [segment](const SegmentSymbol& symbol) {
                return symbol.segment == segment;
            };
            if (!std::ranges::any_of(variant.segments, isSet) &&
                !std::ranges::any_of(part.segments, isSet)) {
                maker.add(SegmentStep{.segment = segment, .symbol = std::string(EmptySegment)});
            }
        }
        ownSegments.clear();
        std::ranges::transform(part.segments, std::back_inserter(ownSegments),
                               &SegmentSymbol::segment);
        maker.add(TransformStep{.placement = part.placement,
                                .isBillboard =
                                    part.isBillboard.value_or(variant.isBillboard.value_or(false)),
                                .resets = true});
        maker.addColors(DrawColors{
            .primitive = part.colors.primitive ? part.colors.primitive : variant.colors.primitive,
            .environment =
                part.colors.environment ? part.colors.environment : variant.colors.environment});
        maker.addSegments(variant.segments, std::nullopt);
        maker.addSegments(part.segments, part.object);
        maker.add(DisplayListStep{.name = part.name, .object = part.object, .pass = part.pass});
    }
    return maker.takeSteps();
}

} // namespace toki::actor
