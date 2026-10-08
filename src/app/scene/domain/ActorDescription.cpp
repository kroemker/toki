#include "ActorDescription.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/DrawSteps.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Tables.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::string makeSource(const base::SourceLocation& location) {
    if (location.file.empty()) {
        return {};
    }
    return std::format("  ({}:{})", location.file.filename().string(), location.line);
}

[[nodiscard]] std::string describeSource(const SceneActorModel& actor) {
    switch (actor.source) {
    case ModelSource::Checkout:
        return "From the checkout's actor database";
    case ModelSource::Shipped:
        return "From the shipped actor database";
    case ModelSource::Guessed:
        return actor.entry && actor.entry->guessedFrom
                   ? std::format("Guessed from {}", actor.entry->guessedFrom->file)
                   : std::string("Guessed from its C");
    case ModelSource::None:
        break;
    }
    return "In no actor database, and with no overlay to guess from";
}

[[nodiscard]] std::string describeObject(const std::optional<std::string>& own,
                                         const std::optional<std::string>& inherited) {
    const std::optional<std::string>& object = own ? own : inherited;
    return object ? std::format(" in {}", *object) : std::string();
}

[[nodiscard]] std::string describePass(const actor::Pass pass) {
    return pass == actor::Pass::Opaque ? "Opaque" : "Translucent";
}

void describeStep(const actor::DrawStep& step, const std::optional<std::string>& object,
                  const std::string_view indent, std::vector<std::string>& lines) {
    if (const auto* skeleton = std::get_if<actor::SkeletonStep>(&step.step)) {
        lines.push_back(std::format("{}Skeleton {}{}", indent, skeleton->skeleton,
                                    describeObject(skeleton->object, object)));
        lines.push_back(skeleton->animation
                            ? std::format("{}Animation {}{}, frame {}", indent,
                                          *skeleton->animation,
                                          describeObject(skeleton->animationObject, object),
                                          skeleton->frame.value_or(0))
                            : std::format("{}At rest", indent));
        for (const actor::LimbSteps& limb : skeleton->limbs) {
            lines.push_back(std::format("{}Limb {}:", indent, limb.limb));
            for (const actor::DrawStep& limbStep : limb.steps) {
                describeStep(limbStep, object, "    ", lines);
            }
        }
    } else if (const auto* list = std::get_if<actor::DisplayListStep>(&step.step)) {
        lines.push_back(std::format("{}{} {}{}", indent, describePass(list->pass), list->name,
                                    describeObject(list->object, object)));
    } else if (const auto* segment = std::get_if<actor::SegmentStep>(&step.step)) {
        lines.push_back(
            std::format("{}Segment {} at {}", indent, segment->segment, segment->symbol));
    } else if (const auto* setup = std::get_if<actor::SetupStep>(&step.step)) {
        lines.push_back(std::format("{}After sSetupDL[{}]", indent, setup->index));
    } else if (const auto* shadow = std::get_if<actor::ShadowStep>(&step.step)) {
        lines.push_back(std::format("{}Its own shadow, size {}", indent, shadow->shadow.size));
    }
}

void describeVariant(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                     std::vector<std::string>& lines) {
    const std::optional<std::string>& object = variant.object ? variant.object : entry.object;
    const actor::DrawKind kind = variant.draw.value_or(entry.draw.value_or(actor::DrawKind::Model));
    if (kind == actor::DrawKind::Marker) {
        lines.emplace_back("Marked: it has no model by design");
    }
    for (const actor::DrawStep& step : actor::makeDrawSteps(variant)) {
        describeStep(step, object, "", lines);
    }
    for (const actor::ChildActor& child : variant.children) {
        lines.push_back(std::format("Spawns {} with params 0x{:04X}", child.actor, child.params));
    }
    lines.push_back(std::format(
        "Scale {}", variant.scale.value_or(entry.scale.value_or(actor::ActorModel::DefaultScale))));
}

} // namespace

ActorDescription describeActor(const SceneActorModel& actor,
                               const actor::PlacementValues& placement,
                               const std::optional<std::span<const std::string>> loadedObjects) {
    const ActorDrawing* drawing = actor.findDrawing(placement);
    ActorDescription description{.isDrawn = drawing != nullptr && drawing->isDrawn()};
    description.lines.push_back(describeSource(actor));
    if (actor.entry && !actor.entry->name.empty()) {
        description.lines.push_back(actor.entry->name);
    }
    const actor::ActorVariant* variant =
        actor.entry ? actor::findVariant(*actor.entry, placement) : nullptr;
    if (variant != nullptr) {
        describeVariant(*actor.entry, *variant, description.lines);
    } else if (actor.entry && !actor.entry->variants.empty()) {
        description.problems.push_back(
            std::format("no variant of its entry matches its params 0x{:04X}", placement.params));
    }
    if (actor.guess) {
        for (const actor::ModelGap& gap : actor.guess->gaps) {
            description.problems.push_back(gap.diagnostic.message +
                                           makeSource(gap.diagnostic.location));
        }
    }
    for (const base::Diagnostic& diagnostic : actor.diagnostics) {
        description.problems.push_back(diagnostic.message + makeSource(diagnostic.location));
    }
    const std::optional<std::string> object = actor.entry ? actor.entry->object : std::nullopt;
    if (loadedObjects && object && !std::ranges::contains(*loadedObjects, *object)) {
        description.problems.push_back(
            std::format("{} is not in the room's object list, so the game does not spawn the "
                        "actor here",
                        *object));
    }
    return description;
}

std::vector<std::string> listLoadedObjects(const SceneLayer& layer, const usize room,
                                           const hackeroot::Tables& tables) {
    std::vector<std::string> loaded{hackeroot::KeepObjectName};
    const auto addObject = [&tables, &loaded](const s16 id) {
        const auto found =
            std::ranges::find(tables.objects, static_cast<usize>(id), &hackeroot::ObjectEntry::id);
        if (id > 0 && found != tables.objects.end()) {
            loaded.push_back(found->name);
        }
    };
    addObject(layer.header.keepObjectId);
    if (room < layer.rooms.size()) {
        std::ranges::for_each(layer.rooms[room].objects, addObject);
    }
    return loaded;
}

} // namespace toki::scene
